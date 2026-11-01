#!/bin/bash
# test-pic-globals.sh -- check that a shared library's pointers to its own
# exported data can be redirected to an executable's copy of that data.
#
# probe/solib-globals.c defines rec_exported and ptr_exported = &rec_exported,
# the shape of libXt's widgetClassRec and coreWidgetClass.
# probe/solib-main-globals.c uses rec_exported directly, so ld gives the
# program a copy of it (R_68K_COPY), and the library's ptr_exported has to be
# bound to that copy at run time.  That needs three things, each checked here
# with readelf:
#
#   1. the -fPIC object keeps the relocation for ptr_exported against the
#      symbol rec_exported.  An unpatched m68k-elf gas folds it into
#      section+offset (see toolchain/patches/binutils-pic-global-relocs.patch);
#   2. the library's dynamic relocation at ptr_exported is R_68K_32 against
#      rec_exported, not R_68K_RELATIVE;
#   3. the program has R_68K_COPY for rec_exported and exports it, so the
#      runtime linker, which looks in the program first, binds the library's
#      relocation to the program's copy.
#
# Static and hidden records must stay relative, and an object built without
# -fPIC must still have its relocation against the section, as before.
#
# Nothing runs on the host.  The result is staged in build/test-pic-globals/
# with run-pic-globals.sh, for a run on AMIX:
#
#   copy build/test-pic-globals/ to the machine, then there:
#   sh run-pic-globals.sh
#
# Expected there: one line with "(same)" and ending in PASS, then
# "exit status: 0".
set -e
cd "$(dirname "$0")/.."
export LC_ALL=C
[ -f config.sh ] || { echo "test-pic-globals.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
READELF="$TC_PREFIX/bin/$TC_TARGET-readelf"
OUT=build/test-pic-globals
rm -rf "${OUT:?}"
mkdir -p "$OUT"

FAILED=0
pass() { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; FAILED=1; }
check() {  # $1 = what, $2 = file, $3 = pattern that must match
	if grep -qE -- "$3" "$2"; then pass "$1"; else fail "$1"; fi
}
check_not() {  # $1 = what, $2 = file, $3 = pattern that must not match
	if grep -qE -- "$3" "$2"; then fail "$1"; else pass "$1"; fi
}
# The value of a symbol in an object's symbol table, without leading zeros.
symval() {  # $1 = file, $2 = symbol, $3 = --dyn-syms or -s
	"$READELF" -W "$3" "$1" | awk -v s="$2" '$8 == s {sub(/^0+/, "", $2); print $2; exit}'
}
# The type of the relocation at an address, and the symbol it names.
relat() {  # $1 = readelf -r output, $2 = address without leading zeros
	awk -v a="$2" '{o = $1; sub(/^0+/, "", o)} o == a {print $3, $5; exit}' "$1"
}

echo "== the -fPIC object"
"$CC" -O2 -fPIC -c -o "$OUT/solib-globals.o" probe/solib-globals.c
R="$OUT/obj.rel"
"$READELF" -rW "$OUT/solib-globals.o" > "$R"
check "a relocation against rec_exported (ptr_exported)" "$R" ' R_68K_32 +[0-9a-f]+ +rec_exported \+ 0$'
check "relocations against rec_fn (the records' function pointers)" "$R" ' R_68K_32 +[0-9a-f]+ +rec_fn \+ 0$'
check_not "none against rec_hidden, which stays relative" "$R" ' rec_hidden '

echo "== the same object built without -fPIC, unchanged"
"$CC" -O2 -c -o "$OUT/solib-globals-nopic.o" probe/solib-globals.c
"$READELF" -rW "$OUT/solib-globals-nopic.o" > "$R"
check_not "no relocation against rec_exported or rec_fn" "$R" ' (rec_exported|rec_fn) '

echo "== the library"
LIB="$OUT/libsolibglobals.so.1"
"$CC" -shared -Wl,-soname,libsolibglobals.so.1 -o "$LIB" "$OUT/solib-globals.o"
"$READELF" -rW "$LIB" > "$R"
for v in exported static hidden; do
	eval "a_$v=\$(symval \"$LIB\" ptr_$v --dyn-syms)"
done
got=$(relat "$R" "$a_exported")
if [ "$got" = "R_68K_32 rec_exported" ]; then
	pass "ptr_exported: R_68K_32 against rec_exported"
else
	fail "ptr_exported: R_68K_32 against rec_exported (found: ${got:-nothing})"
fi
for v in static hidden; do
	eval "a=\$a_$v"
	got=$(relat "$R" "$a")
	case "$got" in
	R_68K_RELATIVE*) pass "ptr_$v: R_68K_RELATIVE" ;;
	*) fail "ptr_$v: R_68K_RELATIVE (found: ${got:-nothing})" ;;
	esac
done
"$READELF" -d "$LIB" > "$OUT/lib.dyn"
check_not "no text relocations" "$OUT/lib.dyn" 'TEXTREL'

echo "== the program"
PROG="$OUT/solib-main-globals"
"$CC" -O2 -o "$PROG" probe/solib-main-globals.c "$LIB"
"$READELF" -rW "$PROG" > "$R"
check "R_68K_COPY for rec_exported" "$R" ' R_68K_COPY +[0-9a-f]+ +rec_exported \+ 0$'
copy_at=$(awk '$3 == "R_68K_COPY" && $5 == "rec_exported" {sub(/^0+/, "", $1); print $1}' "$R")
def_at=$(symval "$PROG" rec_exported --dyn-syms)
if [ -n "$copy_at" ] && [ "$copy_at" = "$def_at" ]; then
	pass "the program exports its copy (0x$def_at), which the library's relocation binds to"
else
	fail "the program exports its copy (copy at ${copy_at:-?}, exported at ${def_at:-?})"
fi
rm -f "${R:?}" "${OUT:?}/lib.dyn"

cat > "$OUT/run-pic-globals.sh" <<'EOF'
# run-pic-globals.sh -- run on AMIX, in the directory test-pic-globals.sh
# staged.
LD_LIBRARY_PATH=`pwd`
export LD_LIBRARY_PATH
./solib-main-globals
echo "exit status: $?"
EOF

echo
if [ "$FAILED" = 0 ]; then
	echo "test-pic-globals.sh: all checks passed; staged in $OUT for a run on AMIX"
else
	echo "test-pic-globals.sh: FAILED"
	exit 1
fi
