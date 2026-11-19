#!/bin/bash
# test-dynstr-order.sh -- check that ld puts the names of libraries and search
# paths at the start of .dynstr, and that tools/check-dynstr-order.py tells a
# file that has them there from one that does not.
#
# AMIX's runtime linker, when dlopen loads a library with dependencies, reads
# each dependency's DT_NEEDED offsets in the string table of the first object
# of the list (see tools/check-dynstr-order.py).  The offsets stay inside that
# table only if the names come first, as AMIX's own ld writes them;
# toolchain/patches/binutils-dynstr-needed-first.patch makes GNU ld do the
# same.  Built here, with what makes the order harder to keep:
#
#   libdsoa.so.1  a SONAME and a version definition (a version script);
#   libdsob.so.1  NEEDED libdsoa.so.1 with a version reference to it, an
#                 RPATH, a symbol whose name ends in "libdsoa.so.1" (ld shares
#                 the tail of a string, and the NEEDED name must not become
#                 the tail of a symbol name placed after it) and a symbol
#                 "so.1" that is the tail of the library names;
#   dsomain       a program NEEDing both, with an RPATH.
#
# Each must pass the check with its first library name at offset 1.  The
# check must fail on files whose names are not first: AMIX's own
# usr/lib/libc.so.1, whose SONAME is its last string, and, when the host has
# an unpatched GNU ld for m68k (m68k-linux-gnu-ld), libdsob.so.1 linked again
# with it.  Nothing here runs on AMIX.
set -e
cd "$(dirname "$0")/.."
export LC_ALL=C
[ -f config.sh ] || { echo "test-dynstr-order.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
READELF="$TC_PREFIX/bin/$TC_TARGET-readelf"
CHECK="python3 tools/check-dynstr-order.py"
OUT=build/test-dynstr-order
rm -rf "${OUT:?}"
mkdir -p "$OUT"

FAILED=0
pass() { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; FAILED=1; }
check() {  # $1 = what, $2 = file holding the output, $3 = pattern that must match
	if grep -qE -- "$3" "$2"; then pass "$1"; else fail "$1"; fi
}
expect_status() {  # $1 = what, $2 = wanted exit status, $3 = output file, then the command
	local what="$1" want="$2" log="$3" got=0
	shift 3
	"$@" > "$log" 2>&1 || got=$?
	if [ "$got" = "$want" ]; then pass "$what"; else fail "$what (exit $got, want $want)"; sed 's/^/        /' "$log"; fi
}

cat > "$OUT/dsoa.c" <<'EOF'
int dsoa_value(void) { return 7; }
EOF
cat > "$OUT/dsoa.map" <<'EOF'
DSOA_1 { global: dsoa_value; local: *; };
EOF
cat > "$OUT/dsob.c" <<'EOF'
extern int dsoa_value(void);
int dsob_tail __asm__("so.1") = 1;
int dsob_long __asm__("zlibdsoa.so.1") = 2;
int dsob_value(void) { return dsoa_value() + 1; }
EOF
cat > "$OUT/dsomain.c" <<'EOF'
extern int dsob_value(void), dsoa_value(void);
int main(void) { return dsob_value() - dsoa_value() - 1; }
EOF

echo "== building"
"$CC" -O2 -fPIC -c -o "$OUT/dsoa.o" "$OUT/dsoa.c"
"$CC" -O2 -fPIC -c -o "$OUT/dsob.o" "$OUT/dsob.c"
"$CC" -shared -Wl,-soname,libdsoa.so.1 -Wl,--version-script,"$OUT/dsoa.map" \
	-o "$OUT/libdsoa.so.1" "$OUT/dsoa.o"
"$CC" -shared -Wl,-soname,libdsob.so.1 -Wl,-rpath,/usr/local/lib \
	-o "$OUT/libdsob.so.1" "$OUT/dsob.o" "$OUT/libdsoa.so.1"
"$CC" -O2 -o "$OUT/dsomain" "$OUT/dsomain.c" -Wl,-rpath,/usr/local/lib \
	"$OUT/libdsob.so.1" "$OUT/libdsoa.so.1"

echo "== the shapes are there"
"$READELF" -dVW "$OUT/libdsoa.so.1" > "$OUT/a.readelf"
"$READELF" -dVW --dyn-syms "$OUT/libdsob.so.1" > "$OUT/b.readelf"
"$READELF" -dW "$OUT/dsomain" > "$OUT/main.readelf"
check "libdsoa.so.1 has a version definition" "$OUT/a.readelf" 'Name: DSOA_1'
check "libdsob.so.1 NEEDs libdsoa.so.1" "$OUT/b.readelf" '\(NEEDED\) +Shared library: \[libdsoa\.so\.1\]'
check "libdsob.so.1 has an RPATH" "$OUT/b.readelf" '\((RPATH|RUNPATH)\) +Library (rpath|runpath): \[/usr/local/lib\]'
check "libdsob.so.1 has a version reference to DSOA_1" "$OUT/b.readelf" 'Name: DSOA_1'
check "libdsob.so.1 exports zlibdsoa.so.1" "$OUT/b.readelf" ' zlibdsoa\.so\.1$'
check "libdsob.so.1 exports so.1" "$OUT/b.readelf" ' so\.1$'
check "dsomain NEEDs libdsob.so.1" "$OUT/main.readelf" '\(NEEDED\) +Shared library: \[libdsob\.so\.1\]'

echo "== library names first"
for f in libdsoa.so.1 libdsob.so.1 dsomain; do
	expect_status "$f passes the check" 0 "$OUT/$f.check" $CHECK -v "$OUT/$f"
	check "$f: its first library name is at offset 1" "$OUT/$f.check" '^    (NEEDED|SONAME) \[[^]]+\] at 1$'
done
check "libdsob.so.1: SONAME and RPATH in the name block too" "$OUT/libdsob.so.1.check" \
	'^    names end at ([0-9]+) of [0-9]+; first other string at \1$'

echo "== files without the order fail"
expect_status "AMIX's own libc.so.1 (SONAME last) fails" 1 "$OUT/libc.check" \
	$CHECK "$AMIX_SYSROOT/usr/lib/libc.so.1"
check "the offending entry is named" "$OUT/libc.check" 'SONAME \[/usr/lib/libc\.so\.1\] at [0-9]+, after '
HOSTLD="${MODERN_PREFIX:-m68k-linux-gnu-}ld"
if command -v "$HOSTLD" > /dev/null; then
	"$HOSTLD" -shared -soname libdsob.so.1 -rpath /usr/local/lib --hash-style=sysv \
		-o "$OUT/libdsob-hostld.so.1" "$OUT/dsob.o" "$OUT/libdsoa.so.1"
	expect_status "libdsob.so.1 linked by an unpatched $HOSTLD fails" 1 "$OUT/hostld.check" \
		$CHECK "$OUT/libdsob-hostld.so.1"
	check "the offending NEEDED is named" "$OUT/hostld.check" 'NEEDED \[libdsoa\.so\.1\] at [0-9]+, after '
else
	echo "  skip  no $HOSTLD on this host for the unpatched-ld case"
fi
expect_status "a file that is not ELF is an error" 2 "$OUT/notelf.check" $CHECK "$OUT/dsoa.c"
expect_status "a relocatable object is not dynamic and passes" 0 "$OUT/rel.check" $CHECK "$OUT/dsoa.o"

echo
if [ "$FAILED" = 0 ]; then
	echo "test-dynstr-order.sh: all checks passed"
else
	echo "test-dynstr-order.sh: FAILED"
	exit 1
fi
