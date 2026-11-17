#!/bin/bash
# test-kernel-codegen.sh -- compile tests/kernel-codegen/ with GCC 14 and
# GCC 16, compare the code, and run what can run under qemu-m68k.
#
# Usage:  tools/test-kernel-codegen.sh [name=/path/to/m68k-elf-gcc ...]
#
# With no arguments the two compilers are GCC 14 ($TC_PREFIX from config.sh,
# else build/prefix) and GCC 16 (build/gcc16/prefix).  Further compilers can
# be named on the command line; the first two named are the pair that is
# diffed.  Output: build/gcc16/codegen/, with a summary in summary.txt.
#
# Steps, each reported as ok or FAIL:
#  1. every C case compiles (-S and -c) and every supervisor .s assembles with
#     its CPU flag, under each compiler;
#  2. no object holds a 68060-unimplemented 64-bit MULU.L/DIVU.L or a
#     68040/060-only FPU form (tools/check-68060-safe.py --68881);
#  3. the supervisor files disassemble to the instructions they were written
#     with (pmove, ptest, pflush, movec, cinv, cpush, move16, plpa);
#  4. the run cases are linked four ways -- callees from each compiler with
#     callers from each -- and run on a 68030, a 68040 and a 68060 in
#     qemu-m68k; a wrong result or a calling-convention mismatch fails.
#     qemu's 68060 traps the 64-bit MULU.L/DIVU.L as the real one does
#     (measured 2026-11-17, qemu 10.2.1), so that run checks libgcc too.
# Then the disassembly of each case is diffed between the first two compilers.
set -e
export LC_ALL=C
# A trapping test would leave qemu's core file in the working directory.
ulimit -c 0
cd "$(dirname "$0")/.."
REPO="$(pwd)"
[ -f config.sh ] && . ./config.sh
T="$REPO/tests/kernel-codegen"
OUT="$REPO/build/gcc16/codegen"
CFLAGS="${CFLAGS:--O2 -m68020-60 -msoft-float -ffreestanding -fno-common -std=gnu99 -Wall}"

NAMES=() CCS=()
if [ $# -eq 0 ]; then
	set -- "gcc14=${TC_PREFIX:-$REPO/build/prefix}/bin/m68k-elf-gcc" \
	       "gcc16=$REPO/build/gcc16/prefix/bin/m68k-elf-gcc"
fi
for a in "$@"; do
	NAMES+=("${a%%=*}") CCS+=("${a#*=}")
	[ -x "${a#*=}" ] || { echo "test-kernel-codegen.sh: no compiler ${a#*=}" >&2; exit 1; }
done
[ ${#NAMES[@]} -ge 2 ] || { echo "test-kernel-codegen.sh: name two compilers" >&2; exit 1; }

RUN_CASES="muldiv64 structret manyargs inlineasm bitops loops"
CC_CASES="$RUN_CASES kasm"
QEMU=$(command -v qemu-m68k || true)
FAILS=0
rm -rf "${OUT:?}"
mkdir -p "$OUT"
SUMMARY="$OUT/summary.txt"
: > "$SUMMARY"
say() { echo "$*" | tee -a "$SUMMARY"; }
fail() { say "FAIL $*"; FAILS=$((FAILS + 1)); }

# The binutils that go with a driver: same prefix, same target; the first
# compiler's when a trial build was installed without its own.
tool() {
	local t="${1%-gcc}-$2"
	[ -x "$t" ] || t="${CCS[0]%-gcc}-$2"
	echo "$t"
}
# libgcc for a compiler; the first compiler's when a trial build has none.
libgcc() {
	local f; f=$("$1" -print-libgcc-file-name)
	[ -f "$f" ] && { echo "$f"; return; }
	"${CCS[0]}" -print-libgcc-file-name
}

say "kernel code-generation test set, $(date -u +%Y-%m-%dT%H:%MZ)"
say "CFLAGS: $CFLAGS"
for i in "${!NAMES[@]}"; do
	say "${NAMES[$i]}: ${CCS[$i]}"
	say "  $("${CCS[$i]}" --version | head -1)"
done

# ---- 1-3: compile, assemble, 68060 check, supervisor mnemonics ----------
for i in "${!NAMES[@]}"; do
	n=${NAMES[$i]} cc=${CCS[$i]} d="$OUT/${NAMES[$i]}"
	mkdir -p "$d"
	ok=1
	for c in $CC_CASES rt; do
		for f in "$T/$c.c" "$T/${c}_check.c"; do
			[ -f "$f" ] || continue
			b=$(basename "$f" .c)
			if ! "$cc" $CFLAGS -S -o "$d/$b.s" "$f" 2> "$d/$b.err" ||
			   ! "$cc" $CFLAGS -c -o "$d/$b.o" "$f" 2> /dev/null; then
				fail "$n: $b.c does not compile:"; sed 's/^/    /' "$d/$b.err" | tee -a "$SUMMARY"; ok=0
			fi
			[ -s "$d/$b.err" ] && [ $ok = 1 ] && { say "  $n: $b.c warns:"; sed 's/^/    /' "$d/$b.err" | tee -a "$SUMMARY"; }
		done
	done
	"$cc" -c -o "$d/crt0.o" "$T/crt0.s"
	for cpu in 030 040 060; do
		if "$cc" -m68$cpu -c -o "$d/sup$cpu.o" "$T/sup$cpu.s" 2> "$d/sup$cpu.err"; then :; else
			fail "$n: sup$cpu.s does not assemble with -m68$cpu:"; sed 's/^/    /' "$d/sup$cpu.err" | tee -a "$SUMMARY"; ok=0
		fi
	done
	[ $ok = 1 ] && say "ok   $n: all C cases compile, sup030/040/060.s assemble"

	objdump=$(tool "$cc" objdump)
	for o in "$d"/*.o; do
		"$objdump" -d -r "$o" > "${o%.o}.dis"
	done
	if python3 "$REPO/tools/check-68060-safe.py" --68881 \
		$(for c in $CC_CASES rt; do ls "$d/$c.dis" "$d/${c}_check.dis" 2>/dev/null; done) \
		> "$d/check-68060.txt" 2>&1; then
		say "ok   $n: no 64-bit mulu.l/divu.l, no 68040/060-only FPU forms in the C objects"
	else
		fail "$n: 68060 check (see $d/check-68060.txt)"; sed 's/^/    /' "$d/check-68060.txt" | head -20 | tee -a "$SUMMARY"
	fi
	miss=""
	for want in "030:pmove ptestr ptestw pflusha pflush pload movec moves rte stop reset" \
		    "040:cinva cinvl cinvp cpusha cpushl cpushp pflush pflushn pflusha pflushan ptestr ptestw move16 movec" \
		    "060:plpar plpaw cinva cpusha pflush pflushan lpstop movec"; do
		cpu=${want%%:*}
		for m in ${want#*:}; do
			grep -qE "[[:space:]]$m[a-z.]*[[:space:]]|[[:space:]]$m\$" "$d/sup$cpu.dis" || miss="$miss sup$cpu:$m"
		done
	done
	if [ -z "$miss" ]; then say "ok   $n: supervisor instructions disassemble as written"
	else fail "$n: missing from the disassembly:$miss"; fi
	"$(tool "$cc" nm)" -u $(for c in $RUN_CASES kasm; do echo "$d/$c.o"; done) 2>/dev/null \
		| awk '/ U / {print $2}' | sort -u > "$d/libcalls.txt"
	say "     $n: external calls from the cases: $(tr '\n' ' ' < "$d/libcalls.txt")"
done

# ---- 4: run, four ways --------------------------------------------------
if [ -z "$QEMU" ]; then
	say "skip run: no qemu-m68k on this host"
else
	for li in "${!NAMES[@]}"; do for ci in "${!NAMES[@]}"; do
		ln=${NAMES[$li]} cn=${NAMES[$ci]}
		[ $li -ge 2 ] && [ $ci -ge 2 ] && [ $li != $ci ] && continue
		exe="$OUT/run-lib_${ln}-caller_${cn}"
		objs=("$OUT/$cn/crt0.o" "$OUT/$cn/rt.o")
		for c in $RUN_CASES; do objs+=("$OUT/$ln/$c.o" "$OUT/$cn/${c}_check.o"); done
		"$(tool "${CCS[$ci]}" ld)" -o "$exe" -e _start "${objs[@]}" "$(libgcc "${CCS[$ci]}")"
		for cpu in m68030 m68040 m68060; do
			if out=$("$QEMU" -cpu $cpu "$exe" 2>&1); then
				say "ok   run callees=$ln callers=$cn on $cpu"
			else
				fail "run callees=$ln callers=$cn on $cpu"; echo "$out" | sed 's/^/    /' | tee -a "$SUMMARY"
			fi
			echo "$out" > "$exe.$cpu.log"
		done
	done; done
fi

# ---- the difference between the first two compilers ---------------------
a=${NAMES[0]} b=${NAMES[1]}
mkdir -p "$OUT/diff-$a-$b"
say ""
say "code size and per-function instruction counts, $a -> $b (text bytes; insns per function)"
for c in $CC_CASES; do
	for n in "$a" "$b"; do
		# One line per function: name and instruction count, addresses gone.
		awk '/^[0-9a-f]+ <.*>:$/ {f=$2; gsub(/[<>:]/,"",f); next}
		     /^ +[0-9a-f]+:\t/ && f {n[f]++}
		     END {for (k in n) print k, n[k]}' "$OUT/$n/$c.dis" | sort > "$OUT/$n/$c.count"
		sed -E 's/^ +[0-9a-f]+:\t([0-9a-f]{4} ?)+ *\t//; s/^[0-9a-f]{8} </</' "$OUT/$n/$c.dis" \
			| grep -v 'file format' > "$OUT/$n/$c.norm"
	done
	sa=$("$(tool "${CCS[0]}" size)" -A "$OUT/$a/$c.o" | awk '$1==".text"{print $2}')
	sb=$("$(tool "${CCS[1]}" size)" -A "$OUT/$b/$c.o" | awk '$1==".text"{print $2}')
	diff -u "$OUT/$a/$c.norm" "$OUT/$b/$c.norm" > "$OUT/diff-$a-$b/$c.diff" || true
	changed=$(join -a1 -a2 -e0 -o 0,1.2,2.2 "$OUT/$a/$c.count" "$OUT/$b/$c.count" \
		| awk '$2 != $3 {printf " %s %d->%d", $1, $2, $3}')
	say "  $c: $sa -> $sb;${changed:- same instruction counts}"
done
say ""
if [ $FAILS -eq 0 ]; then say "all checks passed"; else say "$FAILS check(s) FAILED"; fi
say "listings, diffs and logs: $OUT"
exit $((FAILS != 0))
