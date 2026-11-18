#!/bin/bash
# test-check-bset-mem.sh -- regression cases for tools/check-bset-mem.py.
#
# Usage:  tools/test-check-bset-mem.sh [path/to/check-bset-mem.py]
#
# Each tests/check-bset-mem/*.s is assembled with the GCC 14 binutils
# ($TC_PREFIX from config.sh, else build/prefix) and the gate is run on the
# object.  The file says what to expect in its comments:
#   | expect: rc=N findings=M [functions=f,g]    the gate's exit status, the
#                                               number of sites, and which
#                                               functions they are in
#   | expect --no-code-ok: rc=N findings=M      the same run with that flag
#   | link: null-section-0                      link it (ld -e _start) and
#                                               point the code section's
#                                               name at string-table offset
#                                               0, as the k30 kernel's is
#                                               (its table starts ".text",
#                                               so readelf shows that name;
#                                               BFD reads offset 0 as none
#                                               and objdump prints
#                                               "Disassembly of section :")
#   | link: strip                               link it and strip every
#                                               symbol, so objdump names no
#                                               labels and prints addresses
#                                               as 0x...
# Output: build/test-check-bset-mem/.  Exit status 0 if every case is as
# expected.
set -e
export LC_ALL=C
cd "$(dirname "$0")/.."
REPO="$(pwd)"
[ -f config.sh ] && . ./config.sh
P="${TC_PREFIX:-$REPO/build/prefix}"
AS="$P/bin/m68k-elf-as" LD="$P/bin/m68k-elf-ld" OBJDUMP="$P/bin/m68k-elf-objdump"
GATE="${1:-$REPO/tools/check-bset-mem.py}"
OUT="$REPO/build/test-check-bset-mem"
rm -rf "${OUT:?}"
mkdir -p "$OUT"
fails=0

check() {  # $1 = case, $2 = object, $3 = expect line, $4.. = gate options
	local name=$1 obj=$2 want=$3 log rc n fn got
	shift 3
	log="$OUT/$name$(echo "$*" | tr -d ' -').log"
	set +e
	python3 "$GATE" --objdump "$OBJDUMP" "$@" "$obj" > "$log" 2>&1
	rc=$?
	set -e
	n=$(sed -n 's/^check-bset-mem.py: \([0-9]*\) site(s).*/\1/p' "$log")
	fn=$(sed -n 's/^[^ ].* <\([^>]*\)>: .*/\1/p' "$log" | sort -u | paste -sd, -)
	got="rc=$rc findings=${n:-0}"
	case $want in *functions=*) got="$got functions=$fn" ;; esac
	if [ "$got" = "$want" ]; then
		echo "ok   $name $*: $got"
	else
		echo "FAIL $name $*: want $want, got $got (see $log)"
		fails=$((fails + 1))
	fi
}

for s in "$REPO"/tests/check-bset-mem/*.s; do
	name=$(basename "$s" .s)
	obj="$OUT/$name.o"
	"$AS" -m68020 -o "$obj" "$s"
	if grep -q '^| link: null-section-0' "$s"; then
		"$LD" -e _start -o "$OUT/$name" "$obj"
		obj="$OUT/$name"
		python3 - "$obj" <<'PY'
import struct, sys
p = sys.argv[1]
d = bytearray(open(p, "rb").read())
shoff, = struct.unpack(">I", d[32:36])
shentsize, shnum, shstrndx = struct.unpack(">HHH", d[46:52])
strs = struct.unpack(">IIIIII", d[shoff + shstrndx * shentsize:][:24])
tab = d[strs[4]:strs[4] + strs[5]]
for k in range(1, shnum):
    name = struct.unpack(">I", d[shoff + k * shentsize:][:4])[0]
    if tab[name:].split(b"\0")[0] == b".text":
        o = shoff + k * shentsize
        d[o:o + 4] = struct.pack(">I", 0)
        break
else:
    sys.exit("no .text section")
open(p, "wb").write(d)
PY
		"$OBJDUMP" -d "$obj" | grep -q '^Disassembly of section :' || {
			echo "FAIL $name: objdump does not print an empty section name"; fails=$((fails + 1)); }
	fi
	if grep -q '^| link: strip' "$s"; then
		"$LD" -e _start -s -o "$OUT/$name" "$obj"
		obj="$OUT/$name"
	fi
	want=$(sed -n 's/^| expect: //p' "$s")
	check "$name" "$obj" "$want"
	want=$(sed -n 's/^| expect --no-code-ok: //p' "$s")
	[ -z "$want" ] || check "$name" "$obj" "$want" --no-code-ok
done
if [ $fails -eq 0 ]; then echo "all cases as expected"; else echo "$fails case(s) FAILED"; fi
exit $((fails != 0))
