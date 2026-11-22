#!/bin/bash
# test-amixrt.sh -- check libamixrt.so.1 and build its probes for AMIX.
#
# libamixrt.so.1 holds what a shared library needs and libc.so.1 does not
# export: libamixc99 built position-independent, and our own gettimeofday,
# select, truncate, realpath, interval timers, sysinfo and message tables,
# and the second wave: siglongjmp, vfork, _fpsetround, _fpsetsticky, the
# syslog, getmntent and utmpx groups (docs/TOOLCHAIN.md, "libamixrt.so: the
# runtime a shared library needs").  This checks its shape with readelf and
# nm, compares vfork with libc's member byte for byte, runs _fpsetround,
# _fpsetsticky and siglongjmp beside libc's under qemu-m68k, then builds the
# probes and checks how each is linked:
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
#                         libamixrt.so.1 mapped, to measure what it costs;
#   libc-static,          the second wave's calls with libc's members, and
#   libc-amixrt           through a library, with libamixrt's; run-amixrt.sh
#                         compares their output (probe/libc-calls.c).
#
# It also checks that every program exports environ, which the specs do.
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
strsignal sys_errlist sys_nerr sysinfo truncate usleep vsnprintf
_fpsetround _fpsetsticky fpsetround fpsetsticky siglongjmp vfork
openlog syslog vsyslog closelog setlogmask getmntent getmntany
getutxent getutxid getutxline pututxline setutxent endutxent utmpxname updutmp"
GOT=$("$NM" -D --defined-only "$RT" | awk '{print $3}' | sort | tr '\n' ' ')
WANT=$(echo $EXPECT | tr ' ' '\n' | sort | tr '\n' ' ')
if [ "$GOT" = "$WANT" ]; then
	pass "exports exactly the first and second waves and libamixc99"
else
	fail "exports exactly the first and second waves and libamixc99"
	echo "        got:  $GOT"
	echo "        want: $WANT"
fi
CLASH=$("$NM" -D --defined-only "$RT" | awk '{print $3}' | sort -u | comm -12 - "$LIBC_SYMS")
if [ -z "$CLASH" ]; then pass "defines nothing libc.so.1 exports"; else fail "also in libc.so.1: $(echo $CLASH)"; fi
MISSING=$("$NM" -D --undefined-only "$RT" | awk '$1 == "U" {print $2}' | sort -u | comm -23 - "$LIBC_SYMS")
if [ -z "$MISSING" ]; then pass "takes everything else from libc.so.1"; else fail "undefined and not in libc.so.1: $(echo $MISSING)"; fi
# _cerror takes the error number in %d0, and a first call through its PLT
# entry would reach it with the lazy binder's %d0 instead: errno must be set
# through the GOT (compat/amixrt_syscall.S).
if "$NM" -D --undefined-only "$RT" | awk '{print $NF}' | grep -qx _cerror; then
	fail "never goes to _cerror, whose %d0 the lazy binder changes"
else
	pass "never goes to _cerror, whose %d0 the lazy binder changes"
fi
if "$READELF" -rW "$RT" | grep -qE 'R_68K_GLOB_DAT +[0-9a-f]+ +errno'; then
	pass "errno through a GOT entry filled at load time"
else
	fail "errno through a GOT entry filled at load time"
fi
if python3 tools/check-68060-safe.py --68881 "$RT" > "$OUT/cpu.log" 2>&1; then
	pass "safe on every 68020..68060 with a 68881 or 68882"
else
	fail "safe on every 68020..68060 with a 68881 or 68882"
	sed 's/^/        /' "$OUT/cpu.log"
fi
rm -f "${R:?}" "${OUT:?}/cpu.log"
# vfork has to be libc's entry exactly (compat/amixrt_vfork.S): the same
# text, byte for byte, with the failure branch going to __amixrt_cerror where
# libc's goes to _cerror.  Both objects leave the branch's field to the
# relocation, so the bytes compare equal.
VF="$OUT/vfork-cmp"
mkdir -p "$VF"
(cd "$VF" && "$TC_PREFIX/bin/$TC_TARGET-ar" x "$AMIX_SYSROOT/usr/ccs/lib/libc.so" vfork.o)
"$CC" -c -o "$VF/ours.o" compat/amixrt_vfork.S
"$TC_PREFIX/bin/$TC_TARGET-objcopy" -O binary -j .text "$VF/vfork.o" "$VF/libc.bin"
"$TC_PREFIX/bin/$TC_TARGET-objcopy" -O binary -j .text "$VF/ours.o" "$VF/ours.bin"
if cmp -s "$VF/libc.bin" "$VF/ours.bin" \
   && "$READELF" -rW "$VF/ours.o" | grep -qE 'R_68K_PC32 +0+ +__amixrt_cerror'; then
	pass "vfork is libc's code byte for byte, failing to __amixrt_cerror"
else
	fail "vfork is libc's code byte for byte, failing to __amixrt_cerror"
fi
rm -rf "${VF:?}"

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
lib libsolibsc.so.1 probe/libc-calls.c
lib libsolibscmid.so.1 probe/libc-calls-mid.c "$OUT/libsolibsc.so.1"
"$CC" -O2 -DLC_VIA_LIBRARY -o "$OUT/libc-amixrt" probe/libc-calls-main.c probe/libc-calls-putmsg.c \
	"$OUT/libsolibscmid.so.1" -Wl,-rpath-link,"$OUT"
"$CC" -O2 -o "$OUT/libc-static" probe/libc-calls-main.c probe/libc-calls.c probe/libc-calls-putmsg.c
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
	'gettimeofday|select|truncate|ftruncate|realpath|setitimer|getitimer|sys_errlist|_sys_siglist|snprintf|usleep|strsignal|sysinfo' || true)
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

echo "== under qemu-m68k: _fpsetround, _fpsetsticky and siglongjmp against libc's"
# They make no system call, so libc's members from the sysroot and ours can
# run side by side under qemu's Linux user emulation, renamed, with the
# registers loaded alike before each call (tests/amixrt-qemu/libc-diff.c).
QEMU=$(command -v qemu-m68k || true)
if [ -z "$QEMU" ]; then
	echo "  skip  no qemu-m68k on this host"
else
	Q="$OUT/qemu"
	OBJCOPY="$TC_PREFIX/bin/$TC_TARGET-objcopy"
	mkdir -p "$Q"
	(cd "$Q" && "$TC_PREFIX/bin/$TC_TARGET-ar" x "$AMIX_SYSROOT/usr/ccs/lib/libc.so" \
		fpsetrnd.o fpsetsticky.o siglongjmp.o)
	"$OBJCOPY" --redefine-sym _fpsetround=libc_fpsetround --redefine-sym fpsetround=libc_fpsetround_w \
		"$Q/fpsetrnd.o"
	"$OBJCOPY" --redefine-sym _fpsetsticky=libc_fpsetsticky \
		--redefine-sym fpsetsticky=libc_fpsetsticky_w "$Q/fpsetsticky.o"
	"$OBJCOPY" --redefine-sym siglongjmp=libc_siglongjmp "$Q/siglongjmp.o"
	"$CC" -O2 -c -o "$Q/ours_fpset.o" compat/amixrt_fpset.c
	"$OBJCOPY" --redefine-sym _fpsetround=ours_fpsetround --redefine-sym fpsetround=ours_fpsetround_w \
		--redefine-sym _fpsetsticky=ours_fpsetsticky --redefine-sym fpsetsticky=ours_fpsetsticky_w \
		"$Q/ours_fpset.o"
	"$CC" -O2 -c -o "$Q/ours_siglongjmp.o" compat/amixrt_siglongjmp.c
	"$OBJCOPY" --redefine-sym siglongjmp=ours_siglongjmp "$Q/ours_siglongjmp.o"
	"$CC" -O2 -ffreestanding -fno-builtin -c -o "$Q/libc-diff.o" tests/amixrt-qemu/libc-diff.c
	"$CC" -c -o "$Q/crt0.o" tests/wrong-code/crt0.s
	"$TC_PREFIX/bin/$TC_TARGET-ld" -o "$Q/libc-diff" -e _start "$Q/crt0.o" "$Q/libc-diff.o" \
		"$Q/ours_fpset.o" "$Q/ours_siglongjmp.o" "$Q/fpsetrnd.o" "$Q/fpsetsticky.o" "$Q/siglongjmp.o"
	for cpu in m68040 m68060; do
		if (ulimit -c 0; "$QEMU" -cpu $cpu "$Q/libc-diff") > "$Q/$cpu.log" 2>&1 \
		   && grep -q '^all cases agree with libc$' "$Q/$cpu.log"; then
			pass "the same results, registers and context as libc's on qemu's $cpu"
		else
			fail "the same results, registers and context as libc's on qemu's $cpu"
			head -5 "$Q/$cpu.log" | sed 's/^/        /'
		fi
	done
	rm -rf "${Q:?}"
fi

echo "== libc-static and libc-amixrt: libc's members against libamixrt's"
WAVE2="siglongjmp vfork _fpsetround _fpsetsticky openlog syslog closelog setlogmask
getmntent getmntany getutxent getutxid getutxline pututxline setutxent endutxent utmpxname"
if [ "$(needed "$OUT/libc-amixrt" | grep -v "^/usr/lib/libc\.so\.1")" = libsolibscmid.so.1 ]; then
	pass "libc-amixrt NEEDs only libsolibscmid and libc.so.1"
else
	fail "libc-amixrt NEEDs only libsolibscmid and libc.so.1"
fi
if needed "$OUT/libsolibsc.so.1" | grep -qx libamixrt.so.1; then
	pass "libsolibsc NEEDs libamixrt.so.1"
else
	fail "libsolibsc NEEDs libamixrt.so.1"
fi
OWN="" MISS=""
for s in $WAVE2; do
	"$NM" --defined-only "$OUT/libc-amixrt" | awk '{print $3}' | grep -qx "$s" && OWN="$OWN $s"
	"$NM" --defined-only "$OUT/libc-static" | awk '{print $3}' | grep -qx "$s" || MISS="$MISS $s"
done
if [ -z "$OWN" ]; then
	pass "libc-amixrt has none of them, so libamixrt's are the ones used"
else
	fail "libc-amixrt has its own:$OWN"
fi
if [ -z "$MISS" ]; then
	pass "libc-static has libc's members of all of them"
else
	fail "libc-static lacks:$MISS"
fi
for p in libc-static libc-amixrt; do
	for s in putmsg _putmsg; do
		if "$NM" -D --defined-only "$OUT/$p" | awk '{print $3}' | grep -qx "$s"; then
			pass "$p exports its own $s, which syslog reaches"
		else
			fail "$p exports its own $s, which syslog reaches"
		fi
	done
done
"$NM" -D --defined-only "$RT" | awk '{print $3}' | sort -u > "$OUT/rt.syms"
SC=$("$NM" -D --undefined-only "$OUT/libsolibsc.so.1" | awk '$1 == "U" {print $2}' | sort -u \
	| comm -23 - "$LIBC_SYMS" | comm -23 - "$OUT/rt.syms")
if [ -z "$SC" ]; then
	pass "libsolibsc takes everything from libc.so.1 and libamixrt.so.1"
else
	fail "libsolibsc has undefined:$(echo $SC)"
fi
rm -f "${OUT:?}/rt.syms"

echo "== every program exports environ"
# The specs export environ and _environ from every program
# (toolchain/install-specs.sh), so a library that refers to environ finds the
# program's, however it is loaded.  nullread uses neither.
for p in nullread solib-dlopen libc-amixrt; do
	for s in environ _environ; do
		if "$NM" -D --defined-only "$OUT/$p" | awk '{print $3}' | grep -qx "$s"; then
			pass "$p exports $s"
		else
			fail "$p exports $s"
		fi
	done
done

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
	 "$OUT"/ram-plain "$OUT"/ram-amixrt "$OUT"/nullread "$OUT"/libc-static "$OUT"/libc-amixrt; do
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
for f in "$OUT"/*.so.1 "$OUT"/solib-main-rt "$OUT"/solib-main-interpose "$OUT"/solib-dlopen "$OUT"/ram-plain \
	 "$OUT"/ram-amixrt "$OUT"/nullread "$OUT"/libc-amixrt; do
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
echo "== libc-static, then libc-amixrt: libc's members, then libamixrt's"
./libc-static > libc-static.out 2>&1
echo "libc-static exit status: $?"
./libc-amixrt > libc-amixrt.out 2>&1
echo "libc-amixrt exit status: $?"
cat libc-amixrt.out
if cmp -s libc-static.out libc-amixrt.out; then
	echo "libc-calls: libamixrt's output is libc's: PASS"
else
	echo "libc-calls: the outputs differ (libc's <, libamixrt's >): FAIL"
	diff libc-static.out libc-amixrt.out
fi
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
