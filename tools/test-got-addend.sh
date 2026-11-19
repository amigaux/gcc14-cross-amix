#!/bin/bash
# test-got-addend.sh -- check that tools/check-got-addend.py finds a GOT-offset
# relocation with an addend, and only that.
#
# `sym+2@GOT` loads 4 bytes from two bytes past sym's GOT slot instead of
# sym's address plus 2, and nothing in the toolchain warns (see the script).
# A synthetic object carries the form in both sizes; it must be found in the
# object and in an archive holding it.  Real -fPIC and -fpic code from this
# compiler, which reaches into extern arrays and structs and carries the
# PC-relative R_68K_GOT32 with addend 2 that loads the GOT pointer, must pass,
# as must the objects of tools/test-shared.sh's libraries.  ld drops the
# addend when it links -- checked here too, since that is why only objects
# can be checked -- so a shared object must be reported as not checkable,
# even one linked with -Wl,--emit-relocs, rather than pass quietly.
# Nothing here runs on AMIX.
set -e
cd "$(dirname "$0")/.."
export LC_ALL=C
[ -f config.sh ] || { echo "test-got-addend.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
AS="$TC_PREFIX/bin/$TC_TARGET-as"
AR="$TC_PREFIX/bin/$TC_TARGET-ar"
OBJDUMP="$TC_PREFIX/bin/$TC_TARGET-objdump"
CHECK="python3 tools/check-got-addend.py"
OUT=build/test-got-addend
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

# What GCC writes for a global in PIC code, plus the two wrong forms.
cat > "$OUT/addend.s" <<'EOF'
	.text
	.globl	addend_fn
addend_fn:
	move.l	%a5,-(%sp)
	lea	(%pc, _GLOBAL_OFFSET_TABLE_@GOTPC), %a5
	move.l	ext_rec@GOT(%a5),%a0
	move.l	ext_rec+2@GOT(%a5),%a1
	move.l	(ext_tab+4)@GOT.w(%a5),%a1
	move.l	(%sp)+,%a5
	rts
EOF
cat > "$OUT/clean.c" <<'EOF'
extern int ext_tab[8];
extern struct { short a; int b; char c[6]; } ext_rec;
static int local_tab[4];
int glob;
int clean_sum(int i) { return ext_tab[1] + ext_tab[i] + ext_rec.b + ext_rec.c[3] + local_tab[i & 3] + glob; }
int *clean_addr(void) { return &ext_tab[3]; }
char *clean_field(void) { return &ext_rec.c[2]; }
EOF

echo "== building"
"$AS" -m68020 --pic -o "$OUT/addend.o" "$OUT/addend.s"
"$AR" rc "$OUT/libaddend.a" "$OUT/addend.o"
"$CC" -shared -o "$OUT/libaddend.so.1" "$OUT/addend.o"
"$CC" -shared -Wl,--emit-relocs -o "$OUT/libaddend-relocs.so.1" "$OUT/addend.o"
"$CC" -O2 -fPIC -c -o "$OUT/clean-fPIC.o" "$OUT/clean.c"
"$CC" -O2 -fpic -c -o "$OUT/clean-fpic.o" "$OUT/clean.c"
"$CC" -O2 -m68060 -fPIC -c -o "$OUT/clean-060.o" "$OUT/clean.c"
"$CC" -O2 -fPIC -c -o "$OUT/solib-c.o" probe/solib-c.c
"$CC" -O2 -fPIC -c -o "$OUT/solib-cxx.o" probe/solib-cxx.cpp
"$CC" -O2 -fPIC -c -o "$OUT/solib-dep.o" probe/solib-dep.c
"$CC" -O2 -fPIC -c -o "$OUT/solib-mid.o" probe/solib-mid.c

echo "== an addend on a GOT-offset relocation is found"
expect_status "the object fails" 1 "$OUT/obj.check" $CHECK "$OUT/addend.o"
check "  R_68K_GOT32O ext_rec+2 is listed" "$OUT/obj.check" '^HIT  .*addend\.o: \.text\+0x[0-9a-f]+: R_68K_GOT32O ext_rec\+2$'
check "  R_68K_GOT16O ext_tab+4 is listed" "$OUT/obj.check" '^HIT  .*addend\.o: \.text\+0x[0-9a-f]+: R_68K_GOT16O ext_tab\+4$'
check "  ext_rec@GOT without an addend is not" "$OUT/obj.check" ', 2 with an addend$'
expect_status "the archive fails" 1 "$OUT/ar.check" $CHECK "$OUT/libaddend.a"
check "  the member is named" "$OUT/ar.check" '^HIT  .*libaddend\.a\(addend\.o\): '

echo "== ld drops the addend, so a linked file is not passed quietly"
# addend_fn's first two GOT loads, ext_rec@GOT and ext_rec+2@GOT: the same
# 32-bit displacement from %a5 means the +2 is gone.
"$OBJDUMP" -d "$OUT/libaddend.so.1" | awk '/<addend_fn>:/ {f = 1} f && /%a5@\(/ {print; if (++n == 2) exit}' > "$OUT/so.dis"
DISP=$(sed -n 's/.*%a5@(\([0-9a-fx]*\)).*/\1/p' "$OUT/so.dis" | sort -u)
if [ "$(wc -l < "$OUT/so.dis")" = 2 ] && [ "$(echo "$DISP" | wc -l)" = 1 ] && [ -n "$DISP" ]; then
	pass "ext_rec@GOT and ext_rec+2@GOT load the same slot"
else
	fail "ext_rec@GOT and ext_rec+2@GOT load the same slot"
	sed 's/^/        /' "$OUT/so.dis"
fi
expect_status "a shared object is not checkable" 2 "$OUT/so.check" $CHECK "$OUT/libaddend.so.1"
expect_status "  nor one linked with --emit-relocs" 2 "$OUT/relocs.check" $CHECK "$OUT/libaddend-relocs.so.1"
expect_status "  --skip-unrelocated makes it a skip" 0 "$OUT/so-skip.check" \
	$CHECK --skip-unrelocated "$OUT/libaddend.so.1"

echo "== this compiler's PIC code passes"
expect_status "-fPIC, -fpic and -m68060 -fPIC objects" 0 "$OUT/clean.check" \
	$CHECK "$OUT/clean-fPIC.o" "$OUT/clean-fpic.o" "$OUT/clean-060.o"
check "  all three are PIC, with GOT-offset relocations" "$OUT/clean.check" \
	': 3 objects, 3 PIC .* \([1-9][0-9]* GOT-offset\), 0 with an addend$'
expect_status "the objects of test-shared.sh's libraries" 0 "$OUT/solib.check" \
	$CHECK "$OUT"/solib-*.o
expect_status "an object that is not m68k ELF is an error" 2 "$OUT/notelf.check" $CHECK "$OUT/clean.c"

echo
if [ "$FAILED" = 0 ]; then
	echo "test-got-addend.sh: all checks passed"
else
	echo "test-got-addend.sh: FAILED"
	exit 1
fi
