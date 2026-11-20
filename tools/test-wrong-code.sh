#!/bin/bash
# test-wrong-code.sh -- the compiler's known wrong-code faults stay fixed.
#
# Usage:  tools/test-wrong-code.sh [path/to/m68k-elf-gcc]
#
# With no argument, the compiler is $TC_PREFIX's from config.sh.  Naming
# another one (an older prefix) shows what the checks catch there.
#
# The cases are in tests/wrong-code/:
#  byteclr  one bit of a byte in memory cleared, set or flipped with a
#           variable bit number (GCC PR target/126121 and the bit-field form
#           at offset 7 - n; toolchain/patches/gcc-0001 and gcc-0002);
#  fmo      two stores indexed by the same count - 1, the first one a
#           memory-to-memory move, which fold-mem-offsets got wrong
#           (toolchain/patches/gcc-0003 and -fno-fold-mem-offsets in the
#           specs).
#
# Steps, each reported as ok or FAIL:
#  1. the driver passes -fno-fold-mem-offsets to cc1 and cc1plus at -O1,
#     -O2, -O3 and -Os, and not when -ffold-mem-offsets is asked for;
#  2. fmo.c at -O2 is the same code with and without -fno-fold-mem-offsets,
#     and with -ffold-mem-offsets the pass deletes no add in store_last
#     (its RTL dump says "Instruction folded" for the miscompile);
#  3. tools/check-bset-mem.py finds nothing in byteclr.o at -O2 or in the
#     runtime libraries (libgcc.a, libstdc++.a, libsupc++.a, libamixc99.a,
#     libamixrt.so.1);
#  4. the cases are built at -O1, -O2, -O3, -Os and -O2 -ffold-mem-offsets,
#     linked with a crt0 for Linux system calls, and run on a 68030, a 68040
#     and a 68060 in qemu-m68k, which checks every stored value.  Nothing
#     here runs on AMIX: the cases call no library.
# Output: build/test-wrong-code/.  Exit status 0 if every check passes.
set -e
cd "$(dirname "$0")/.."
REPO="$(pwd)"
export LC_ALL=C
# A trapping case would leave qemu's core file in the working directory.
ulimit -c 0
[ -f config.sh ] && . ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="${1:-${TC_PREFIX:-$REPO/build/prefix}/bin/$TC_TARGET-gcc}"
[ -x "$CC" ] || { echo "test-wrong-code.sh: no compiler $CC" >&2; exit 1; }
BIN="$(dirname "$CC")"
LD="$BIN/$TC_TARGET-ld" OBJDUMP="$BIN/$TC_TARGET-objdump"
T="$REPO/tests/wrong-code"
OUT="$REPO/build/test-wrong-code"
QEMU=$(command -v qemu-m68k || true)
CFLAGS="-m68020-60 -ffreestanding -fno-common -Wall"
rm -rf "${OUT:?}"
mkdir -p "$OUT"

FAILED=0
pass() { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; FAILED=$((FAILED + 1)); }

echo "== $("$CC" --version | head -1), $CC"

echo "== 1. the driver turns fold-mem-offsets off"
for lang in c c++; do
	for o in -O1 -O2 -O3 -Os; do
		"$CC" -x $lang $o -### -c -o /dev/null /dev/null 2> "$OUT/driver.txt" || true
		if grep -E '/cc1(plus)?"? ' "$OUT/driver.txt" | grep -qE -- ' "?-fno-fold-mem-offsets"?( |$)'; then
			pass "$lang $o: -fno-fold-mem-offsets reaches the compiler"
		else
			fail "$lang $o: -fno-fold-mem-offsets does not reach the compiler"
		fi
	done
	"$CC" -x $lang -O2 -ffold-mem-offsets -### -c -o /dev/null /dev/null 2> "$OUT/driver.txt" || true
	if grep -E '/cc1(plus)?"? ' "$OUT/driver.txt" | grep -qE -- ' "?-fno-fold-mem-offsets"?( |$)'; then
		fail "$lang -O2 -ffold-mem-offsets: the driver still turns it off"
	else
		pass "$lang -O2 -ffold-mem-offsets: left on when asked for"
	fi
done

echo "== 2. fmo.c's code"
"$CC" $CFLAGS -O2 -S -o "$OUT/fmo-O2.s" "$T/fmo.c"
"$CC" $CFLAGS -O2 -fno-fold-mem-offsets -S -o "$OUT/fmo-nofmo.s" "$T/fmo.c"
if cmp -s "$OUT/fmo-O2.s" "$OUT/fmo-nofmo.s"; then
	pass "-O2 is the code of -O2 -fno-fold-mem-offsets"
else
	fail "-O2 differs from -O2 -fno-fold-mem-offsets (diff $OUT/fmo-O2.s $OUT/fmo-nofmo.s)"
fi
"$CC" $CFLAGS -O2 -ffold-mem-offsets -S -o "$OUT/fmo-fmo.s" \
	-fdump-rtl-fold_mem_offsets="$OUT/fmo-fmo.dump" "$T/fmo.c"
n=$(grep -c '^Instruction folded' "$OUT/fmo-fmo.dump" || true)
if [ "$n" = 0 ]; then
	pass "-O2 -ffold-mem-offsets: no add deleted in store_last"
else
	fail "-O2 -ffold-mem-offsets: $n add(s) deleted in store_last (see $OUT/fmo-fmo.dump)"
	grep -E 'move\.l [0-9]+\(%a[0-7]\),\(%a[0-7],%d[0-7]\.l\)' "$OUT/fmo-fmo.s" | sed 's/^/        /'
fi

echo "== 3. no PR 126121 pattern (check-bset-mem.py)"
"$CC" $CFLAGS -O2 -c -o "$OUT/byteclr-O2.o" "$T/byteclr.c"
if python3 tools/check-bset-mem.py --objdump "$OBJDUMP" "$OUT/byteclr-O2.o" > "$OUT/bset-byteclr.txt" 2>&1; then
	pass "byteclr.o at -O2"
else
	fail "byteclr.o at -O2 (see $OUT/bset-byteclr.txt)"
	grep -v 'instructions decoded' "$OUT/bset-byteclr.txt" | sed 's/^/        /' | head -10
fi
LIBGCC=$("$CC" -print-libgcc-file-name)
LIB="$(dirname "$BIN")/$TC_TARGET/lib"
RUNTIME=("$LIBGCC" "$LIB"/libstdc++.a "$LIB"/libsupc++.a "$LIB"/libamixc99.a "$LIB"/amixrt/libamixrt.so.1)
if python3 tools/check-bset-mem.py --objdump "$OBJDUMP" "${RUNTIME[@]}" > "$OUT/bset-runtime.txt" 2>&1; then
	pass "the runtime libraries"
else
	fail "the runtime libraries (see $OUT/bset-runtime.txt)"
fi

echo "== 4. the cases run"
if [ -z "$QEMU" ]; then
	echo "  skip  no qemu-m68k on this host"
else
	"$CC" -c -o "$OUT/crt0.o" "$T/crt0.s"
	for flags in "-O1" "-O2" "-O3" "-Os" "-O2 -ffold-mem-offsets"; do
		tag=$(echo "$flags" | tr -d ' ')
		d="$OUT/run$tag"
		mkdir -p "$d"
		objs=("$OUT/crt0.o")
		for c in rt byteclr byteclr_check fmo fmo_check; do
			"$CC" $CFLAGS $flags -c -o "$d/$c.o" "$T/$c.c"
			objs+=("$d/$c.o")
		done
		"$LD" -o "$d/cases" -e _start "${objs[@]}" "$LIBGCC"
		for cpu in m68030 m68040 m68060; do
			if "$QEMU" -cpu $cpu "$d/cases" > "$d/$cpu.log" 2>&1 \
			   && grep -q '^all cases passed$' "$d/$cpu.log"; then
				pass "$flags on $cpu"
			else
				fail "$flags on $cpu: $(grep -c '^FAIL line' "$d/$cpu.log" || true) wrong result(s), $(grep -E '^[a-z]+: FAILED' "$d/$cpu.log" | tr '\n' ' ')"
			fi
		done
	done
fi

echo
if [ "$FAILED" = 0 ]; then
	echo "test-wrong-code.sh: all checks passed"
else
	echo "test-wrong-code.sh: $FAILED check(s) FAILED"
	exit 1
fi
