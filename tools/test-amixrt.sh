#!/bin/bash
# test-amixrt.sh -- check libamixrt.so.1 and build its probes for AMIX.
#
# libamixrt.so.1 holds what a shared library needs and libc.so.1 does not
# export: libamixc99 built position-independent, and our own gettimeofday,
# select, truncate, realpath, interval timers and message tables
# (docs/TOOLCHAIN.md, "libamixrt.so: the runtime a shared library needs").
# This checks its shape with readelf and nm, then builds four probes and
# checks how each is linked:
#
#   solib-main-rt         every libamixrt function, called from a library the
#                         program reaches only through another one, so the
#                         copies used are libamixrt's (probe/solib-main-rt.c);
#   solib-main-interpose  drand48, syslog's mask and the interval timers set
#                         in the program and read in such a library, and the
#                         other way round: the program's copies must win
#                         (probe/solib-main-interpose.c);
#   solib-dlopen          a library stack loaded with dlopen by a program
#                         that links none of it (probe/solib-dlopen.c);
#   ram-plain, ram-amixrt the same sleeping program, with and without
#                         libamixrt.so.1 mapped, to measure what it costs.
#
# Nothing runs on the host.  Everything is staged in build/test-amixrt/ with
# run-amixrt.sh and ram-measure.sh; docs/shared-library-tests.md says how to
# run them on the machine and what to expect.
set -e
cd "$(dirname "$0")/.."
export LC_ALL=C
[ -f config.sh ] || { echo "test-amixrt.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
READELF="$TC_PREFIX/bin/$TC_TARGET-readelf"
NM="$TC_PREFIX/bin/$TC_TARGET-nm"
OUT=build/test-amixrt
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
needed() {  # the NEEDED entries of $1, one per line
	"$READELF" -d "$1" | sed -nE 's/.*\(NEEDED\) +Shared library: \[([^]]+)\]/\1/p'
}

RTDIR=$("$CC" -print-file-name=amixrt)
RT="$RTDIR/libamixrt.so.1"
[ -f "$RT" ] || { echo "test-amixrt.sh: no $RT -- run tools/build-compat.sh" >&2; exit 1; }
cp "$RT" "$OUT/"
LIBC_SYMS="$OUT/libc.so.1.syms"
"$NM" --defined-only "$AMIX_SYSROOT/usr/lib/libc.so.1" | awk '$2 ~ /^[A-Z]$/ {print $3}' | sort -u > "$LIBC_SYMS"

echo "== libamixrt.so.1"
R="$OUT/rt.readelf"
"$READELF" -hldSW "$RT" > "$R"
check "a shared object (ET_DYN)" "$R" 'Type: +DYN'
check "SONAME libamixrt.so.1" "$R" '\(SONAME\) +Library soname: \[libamixrt\.so\.1\]'
check_not "no program interpreter" "$R" 'INTERP|Requesting program interpreter'
check_not "no NEEDED: never libc.so.1, which is always there" "$R" '\(NEEDED\)'
check_not "no text relocations" "$R" 'TEXTREL'
check_not "no initialisation: no DT_INIT, DT_FINI or init arrays" "$R" \
	'\((INIT|FINI|INIT_ARRAY|FINI_ARRAY|INIT_ARRAYSZ|FINI_ARRAYSZ)\)|\.ctors|\.dtors|\.init_array|\.fini_array'
EXPECT="__cxa_atexit __cxa_finalize __wrap_dlopen _sys_errlist _sys_nerr _sys_nsig _sys_siglist
amix_snprintf amix_usleep amix_vsnprintf ftruncate getitimer gettimeofday
realpath select setitimer settimeofday snprintf strcasecmp strncasecmp
strsignal sys_errlist sys_nerr truncate usleep vsnprintf"
GOT=$("$NM" -D --defined-only "$RT" | awk '{print $3}' | sort | tr '\n' ' ')
WANT=$(echo $EXPECT | tr ' ' '\n' | sort | tr '\n' ' ')
if [ "$GOT" = "$WANT" ]; then
	pass "exports exactly the first wave and libamixc99"
else
	fail "exports exactly the first wave and libamixc99"
	echo "        got:  $GOT"
	echo "        want: $WANT"
fi
CLASH=$("$NM" -D --defined-only "$RT" | awk '{print $3}' | sort -u | comm -12 - "$LIBC_SYMS")
if [ -z "$CLASH" ]; then pass "defines nothing libc.so.1 exports"; else fail "also in libc.so.1: $(echo $CLASH)"; fi
MISSING=$("$NM" -D --undefined-only "$RT" | awk '$1 == "U" {print $2}' | sort -u | comm -23 - "$LIBC_SYMS")
if [ -z "$MISSING" ]; then pass "takes everything else from libc.so.1"; else fail "undefined and not in libc.so.1: $(echo $MISSING)"; fi
if python3 tools/check-68060-safe.py --68881 "$RT" > "$OUT/cpu.log" 2>&1; then
	pass "safe on every 68020..68060 with a 68881 or 68882"
else
	fail "safe on every 68020..68060 with a 68881 or 68882"
	sed 's/^/        /' "$OUT/cpu.log"
fi
rm -f "${R:?}" "${OUT:?}/cpu.log"

lib() {  # $1 = soname, $2 = source, the rest = libraries to link against
	local so="$1" src="$2"
	shift 2
	"$CC" -O2 -fPIC -shared -Wl,-soname,"$so" -o "$OUT/$so" "$src" "$@"
}

echo "== building the probes"
lib libsolibdep.so.1 probe/solib-dep.c
lib libsolibmid.so.1 probe/solib-mid.c "$OUT/libsolibdep.so.1"
lib libsolibrt.so.1 probe/solib-rt.c
lib libsolibrtmid.so.1 probe/solib-rtmid.c "$OUT/libsolibrt.so.1"
lib libsolibstate.so.1 probe/solib-state.c
lib libsolibstatemid.so.1 probe/solib-statemid.c "$OUT/libsolibstate.so.1"
lib libsolibram.so.1 probe/solib-ram.c
"$CC" -O2 -fPIC -shared -DUSE_AMIXRT -Wl,-soname,libsolibramrt.so.1 \
	-o "$OUT/libsolibramrt.so.1" probe/solib-ram.c
"$CC" -O2 -o "$OUT/solib-main-rt" probe/solib-main-rt.c "$OUT/libsolibrtmid.so.1" \
	-Wl,-rpath-link,"$OUT"
"$CC" -O2 -o "$OUT/solib-main-interpose" probe/solib-main-interpose.c "$OUT/libsolibstatemid.so.1" \
	-Wl,-rpath-link,"$OUT"
"$CC" -O2 -o "$OUT/solib-dlopen" probe/solib-dlopen.c -ldl
"$CC" -O2 -o "$OUT/nullread" probe/nullread.c
"$CC" -O2 -o "$OUT/ram-plain" probe/solib-main-ram.c "$OUT/libsolibram.so.1"
"$CC" -O2 -o "$OUT/ram-amixrt" probe/solib-main-ram.c "$OUT/libsolibramrt.so.1"
pass "all built"

echo "== solib-main-rt: libamixrt's own copies"
P="$OUT/solib-main-rt"
if needed "$OUT/libsolibrt.so.1" | grep -qx libamixrt.so.1; then
	pass "libsolibrt NEEDs libamixrt.so.1"
else
	fail "libsolibrt NEEDs libamixrt.so.1"
fi
if needed "$P" | grep -q 'libamixrt\|libsolibrt\.'; then
	fail "the program NEEDs only libsolibrtmid"
else
	pass "the program NEEDs only libsolibrtmid"
fi
OWN=$("$NM" --defined-only "$P" | awk '{print $3}' | grep -xE \
	'gettimeofday|select|truncate|ftruncate|realpath|setitimer|getitimer|sys_errlist|_sys_siglist|snprintf|usleep|strsignal' || true)
if [ -z "$OWN" ]; then
	pass "the program has no copy of its own, so libamixrt's are the ones used"
else
	fail "the program has its own: $(echo $OWN)"
fi

echo "== solib-main-interpose: the program's copies win"
P="$OUT/solib-main-interpose"
for s in lrand48 srand48 setlogmask setitimer getitimer; do
	if "$NM" -D --defined-only "$P" | awk '{print $3}' | grep -qx "$s"; then
		pass "the program defines and exports $s"
	else
		fail "the program defines and exports $s"
	fi
done
if needed "$OUT/libsolibstate.so.1" | grep -qx libamixrt.so.1; then
	pass "libsolibstate NEEDs libamixrt.so.1, which has a setitimer too"
else
	fail "libsolibstate NEEDs libamixrt.so.1, which has a setitimer too"
fi

echo "== solib-dlopen: a stack the program does not link"
P="$OUT/solib-dlopen"
if needed "$P" | grep -q 'libsolib\|libamixrt'; then
	fail "the program NEEDs none of the libraries"
else
	pass "the program NEEDs none of the libraries"
fi
for s in __amix_dso_init __amix_dso_batch; do
	if "$NM" -D --defined-only "$P" | awk '{print $3}' | grep -qx "$s"; then
		pass "the program exports $s, so the libraries it opens find the runner"
	else
		fail "the program exports $s, so the libraries it opens find the runner"
	fi
done
if "$NM" --defined-only "$P" | awk '{print $3}' | grep -qx __wrap_dlopen \
   && ! "$NM" -u "$P" | awk '{print $NF}' | grep -qx dlopen; then
	pass "its dlopen call goes to __wrap_dlopen, which orders the constructors"
else
	fail "its dlopen call goes to __wrap_dlopen, which orders the constructors"
fi

echo "== ram-plain and ram-amixrt"
if needed "$OUT/libsolibram.so.1" | grep -q .; then
	fail "libsolibram NEEDs nothing"
else
	pass "libsolibram NEEDs nothing"
fi
if [ "$(needed "$OUT/libsolibramrt.so.1")" = libamixrt.so.1 ]; then
	pass "libsolibramrt NEEDs libamixrt.so.1 and nothing else"
else
	fail "libsolibramrt NEEDs libamixrt.so.1 and nothing else"
fi

echo "== segments map right with 2 KiB, 4 KiB and 8 KiB pages"
# AMIX's 68030 kernels map in 2 KiB pages, the 68060 port's in 4 KiB.  A
# segment maps right on both when its file offset and address agree modulo
# the largest page, and the writable segment starts on a later 2 KiB page
# than the text ends on, so the two mappings never share a page.
for f in "$OUT"/*.so.1 "$OUT"/solib-main-rt "$OUT"/solib-main-interpose "$OUT"/solib-dlopen \
	 "$OUT"/ram-plain "$OUT"/ram-amixrt "$OUT"/nullread; do
	# POSIX awk: no strtonum, so the hex is read here.
	"$READELF" -lW "$f" | awk -v f="$(basename "$f")" '
		function hex(s,   v, i) {
			s = tolower(s)
			sub(/^0x/, "", s)
			v = 0
			for (i = 1; i <= length(s); i++)
				v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
			return v
		}
		$1 == "LOAD" {
			off = hex($2); va = hex($3); fsz = hex($5)
			if (off % 8192 != va % 8192) print f ": offset and address disagree modulo 8 KiB"
			n++
			if (n == 1) textend = va + fsz
			else if (int(va / 2048) <= int((textend - 1) / 2048)) print f ": data starts on the text segment'"'"'s last 2 KiB page"
		}' >> "$OUT/layout.log"
done
if [ -s "$OUT/layout.log" ]; then
	fail "segment layout"
	sed 's/^/        /' "$OUT/layout.log"
else
	pass "every file"
fi
rm -f "${OUT:?}/layout.log"

echo "== no text relocations anywhere"
TR=""
for f in "$OUT"/*.so.1 "$OUT"/solib-main-rt "$OUT"/solib-main-interpose "$OUT"/solib-dlopen "$OUT"/ram-plain "$OUT"/ram-amixrt "$OUT"/nullread; do
	"$READELF" -d "$f" | grep -q TEXTREL && TR="$TR $(basename "$f")"
done
if [ -z "$TR" ]; then pass "none"; else fail "text relocations in:$TR"; fi
rm -f "${LIBC_SYMS:?}"

cat > "$OUT/run-amixrt.sh" <<'EOF'
# run-amixrt.sh -- run on AMIX, in the directory test-amixrt.sh staged.
LD_LIBRARY_PATH=`pwd`
export LD_LIBRARY_PATH
for p in solib-main-rt solib-main-interpose solib-dlopen; do
	echo "== $p"
	./$p
	echo "exit status: $?"
done
echo "== nullread (a diagnostic of the kernel, not a test)"
./nullread
EOF

cat > "$OUT/ram-measure.sh" <<'EOF'
# ram-measure.sh [N] -- run on AMIX as root, in the directory test-amixrt.sh
# staged.  Starts N copies (10 by default) of ram-plain, then of ram-amixrt,
# and prints the kernel's free page count before and while they run, and
# ps -l for the first few.  freemem is in pages of 4 KB.
N=${1:-10}
LD_LIBRARY_PATH=`pwd`
export LD_LIBRARY_PATH
freemem() {
	echo "freemem/D" | adb -k /stand/unix /dev/kmem | tail -1
}
for p in ram-plain ram-amixrt; do
	sync
	sleep 5
	echo "== $p x $N"
	echo "before:  `freemem`"
	i=0
	while [ $i -lt $N ]; do
		./$p 60 &
		i=`expr $i + 1`
	done
	sleep 15
	echo "running: `freemem`"
	ps -l | sed -n '1p;/ram-/p' | head -4
	wait
done
EOF

echo
if [ "$FAILED" = 0 ]; then
	echo "test-amixrt.sh: all checks passed; staged in $OUT for a run on AMIX"
else
	echo "test-amixrt.sh: FAILED"
	exit 1
fi
