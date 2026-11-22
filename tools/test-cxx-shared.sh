#!/bin/bash
# test-cxx-shared.sh -- the shared C++ runtime, libstdc++.so.6: its shape,
# C++ libraries and programs linked against it, and static C++ programs
# left as they were.
#
# Builds probe/cxxso-a.cpp and probe/cxxso-b.cpp into libcxxsoa.so.1 and
# libcxxsob.so.1 with `g++ -shared`, and links probe/cxxso-main.cpp against
# both twice: with `g++ -shared-libgcc` (cxxso-main), which takes the runtime
# from libstdc++.so.6, and the default way (cxxso-main-static), which carries
# a static runtime of its own.  probe/cxxso-cmain.c, a C program, links
# libcxxsob.so.1 with plain gcc.  Then checks with readelf and nm:
#
#  - libstdc++.so.6 loads on AMIX's runtime linker: a shared object with its
#    SONAME, no interpreter, no text relocations, a SysV hash table and no
#    GNU one, no symbol versions, the library names first in .dynstr, NEEDs
#    only libamixrt.so.1, everything it leaves undefined is exported by
#    libc.so.1 or libamixrt.so.1, and its segments map with 2, 4 and 8 KiB
#    pages;
#  - it exports the C++ ABI and the unwinder, and none of libgcc's helpers
#    or of its own hidden floor and frexpl;
#  - the libraries NEED libstdc++.so.6 and hold no C++ runtime of their own,
#    and have no text relocations;
#  - cxxso-main NEEDs libstdc++.so.6 and defines none of the runtime;
#    cxxso-main-static defines and exports it, which is what libstdc++.so.6
#    stops at start-up; cxxso-cmain exports none of it;
#  - `g++ -shared -static-libstdc++` still makes a library with its own,
#    static copy;
#  - static C++ programs (probe/cxxprobe.cpp, probe/stl.cpp) are what they
#    were: linked the default way and with -static-libstdc++, the same bytes,
#    and nothing NEEDs libstdc++.so.6.  With CXX_STATIC_REF=DIR, they are
#    also compared byte for byte with DIR/cxxprobe and DIR/stl, a copy of
#    build/test-cxx-shared/static/ saved from a run before a change.
#
# Nothing runs on the host.  Staged in build/test-cxx-shared/ with
# run-cxx-shared.sh, for a run on AMIX; docs/shared-library-tests.md says
# what to expect there.
set -e
cd "$(dirname "$0")/.."
export LC_ALL=C
[ -f config.sh ] || { echo "test-cxx-shared.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
CXX="$TC_PREFIX/bin/$TC_TARGET-g++"
READELF="$TC_PREFIX/bin/$TC_TARGET-readelf"
NM="$TC_PREFIX/bin/$TC_TARGET-nm"
OUT=build/test-cxx-shared
rm -rf "${OUT:?}"
mkdir -p "$OUT/static"

CXXRT=$("$CC" -print-file-name=cxxrt)
RTDIR=$("$CC" -print-file-name=amixrt)
for f in "$CXXRT/libstdc++.so.6" "$CXXRT/libstdc++.so" "$CXXRT/libstdc++_nonshared.a" "$RTDIR/libamixrt.so.1"; do
	[ -f "$f" ] || { echo "test-cxx-shared.sh: no $f -- run toolchain/build-cross.sh" >&2; exit 1; }
done
cp "$CXXRT/libstdc++.so.6" "$RTDIR/libamixrt.so.1" "$OUT/"

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
check_dynstr() {  # $1 = file
	if python3 tools/check-dynstr-order.py "$1" > /dev/null; then
		pass "library names first in .dynstr"
	else
		fail "library names first in .dynstr"
	fi
}
# The C++ runtime's own entry points: a file that defines any of them
# carries a copy of the runtime.
RUNTIME_RE='^(__gxx_personality_v0|__cxa_throw|__cxa_begin_catch|__cxa_get_globals|_Unwind_RaiseException|__register_frame_info)$'
defines_runtime() {  # $1 = file: the runtime symbols it defines, any binding
	"$NM" --defined-only "$1" 2>/dev/null | awk '{print $3}' | grep -E "$RUNTIME_RE" | sort -u | tr '\n' ' '
}
LIBC_SYMS="$OUT/libc.so.1.syms"
"$NM" --defined-only "$AMIX_SYSROOT/usr/lib/libc.so.1" | awk '$2 ~ /^[A-Z]$/ {print $3}' | sort -u > "$LIBC_SYMS"

echo "== libstdc++.so.6"
L="$OUT/libstdc++.so.6"
R="$OUT/cxxrt.readelf"
"$READELF" -hlSdW --dyn-syms "$L" > "$R"
check "a shared object (ET_DYN)" "$R" 'Type: +DYN'
check "SONAME libstdc++.so.6" "$R" '\(SONAME\) +Library soname: \[libstdc\+\+\.so\.6\]'
check_not "no program interpreter" "$R" 'INTERP|Requesting program interpreter'
check_not "no text relocations" "$R" 'TEXTREL'
check "a SysV hash table" "$R" '\(HASH\) '
check_not "no GNU hash table" "$R" 'GNU_HASH|\.gnu\.hash'
check_not "no symbol versions" "$R" 'VERSYM|VERNEED|VERDEF|\.gnu\.version'
check "DT_INIT and DT_FINI" "$R" '\(INIT\) '
if [ "$(needed "$L" | tr '\n' ' ')" = "libamixrt.so.1 " ]; then
	pass "NEEDs libamixrt.so.1 and nothing else"
else
	fail "NEEDs libamixrt.so.1 and nothing else: $(needed "$L" | tr '\n' ' ')"
fi
check_dynstr "$L"
"$NM" -D --defined-only "$OUT/libamixrt.so.1" | awk '{print $3}' | sort -u > "$OUT/rt.syms"
MISSING=$("$NM" -D --undefined-only "$L" | awk '$1 == "U" {print $2}' | sort -u \
	| comm -23 - "$LIBC_SYMS" | comm -23 - "$OUT/rt.syms")
if [ -z "$MISSING" ]; then
	pass "everything left undefined is in libc.so.1 or libamixrt.so.1"
else
	fail "undefined and in neither: $(echo $MISSING)"
fi
"$NM" -D --defined-only "$L" | awk '{print $3}' | sort -u > "$OUT/cxxrt.exports"
WANT="_Unwind_RaiseException _Unwind_Resume __register_frame_info __deregister_frame_info
__cxa_throw __cxa_begin_catch __cxa_end_catch __cxa_get_globals __gxx_personality_v0
__dynamic_cast _Znwm _ZdlPv _ZTISt9exception _ZSt9terminatev _ZSt4cout"
LACK=$(echo $WANT | tr ' ' '\n' | sort | comm -23 - "$OUT/cxxrt.exports")
if [ -z "$LACK" ]; then pass "exports the C++ ABI and the unwinder"; else fail "does not export: $(echo $LACK)"; fi
LEAK=$(grep -E '^(__[a-z]*di[0-9]|__[a-z]*si[0-9]|__[a-z]*[dsx]f[0-9]|floor|frexpl|amix_cxxrt_check|__amix_[a-z_]*)$' "$OUT/cxxrt.exports" || true)
if [ -z "$LEAK" ]; then pass "exports no libgcc helper and nothing of amix_cxxrt.o"; else fail "exports: $(echo $LEAK)"; fi
if python3 tools/check-68060-safe.py --68881 "$L" > "$OUT/cpu.log" 2>&1; then
	pass "safe on every 68020..68060 with a 68881 or 68882"
else
	fail "safe on every 68020..68060 with a 68881 or 68882"
	sed 's/^/        /' "$OUT/cpu.log"
fi
# AMIX's 68030 kernels map in 2 KiB pages, the 68060 port's in 4 KiB: the
# same rule as tools/test-amixrt.sh.
"$READELF" -lW "$L" | awk '
	function hex(s,   v, i) {
		s = tolower(s); sub(/^0x/, "", s); v = 0
		for (i = 1; i <= length(s); i++) v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
		return v
	}
	$1 == "LOAD" {
		off = hex($2); va = hex($3); fsz = hex($5)
		if (off % 8192 != va % 8192) print "offset and address disagree modulo 8 KiB"
		if (++n == 1) textend = va + fsz
		else if (int(va / 2048) <= int((textend - 1) / 2048)) print "data starts on the text segment'"'"'s last 2 KiB page"
	}' > "$OUT/layout.log"
if [ -s "$OUT/layout.log" ]; then fail "segments map with 2, 4 and 8 KiB pages: $(cat "$OUT/layout.log")"; else pass "segments map with 2, 4 and 8 KiB pages"; fi
rm -f "${OUT:?}/layout.log"
check "the link-time libstdc++.so names libstdc++.so.6 and the stubs" "$CXXRT/libstdc++.so" \
	'^INPUT \( libstdc\+\+\.so\.6 libstdc\+\+_nonshared\.a \)$'
rm -f "${R:?}" "${OUT:?}/cpu.log"

echo "== building the probes"
"$CXX" -O2 -fPIC -shared -Wl,-soname,libcxxsoa.so.1 -o "$OUT/libcxxsoa.so.1" probe/cxxso-a.cpp
"$CXX" -O2 -fPIC -shared -Wl,-soname,libcxxsob.so.1 -o "$OUT/libcxxsob.so.1" probe/cxxso-b.cpp "$OUT/libcxxsoa.so.1"
"$CXX" -O2 -shared-libgcc -o "$OUT/cxxso-main" probe/cxxso-main.cpp "$OUT/libcxxsob.so.1" "$OUT/libcxxsoa.so.1"
"$CXX" -O2 -o "$OUT/cxxso-main-static" probe/cxxso-main.cpp "$OUT/libcxxsob.so.1" "$OUT/libcxxsoa.so.1"
"$CC" -O2 -o "$OUT/cxxso-cmain" probe/cxxso-cmain.c "$OUT/libcxxsob.so.1" -Wl,-rpath-link,"$OUT"
pass "all built"

for lib in libcxxsoa.so.1 libcxxsob.so.1; do
	echo "== $lib"
	R="$OUT/$lib.readelf"
	"$READELF" -dW "$OUT/$lib" > "$R"
	check "NEEDs libstdc++.so.6" "$R" '\(NEEDED\) +Shared library: \[libstdc\+\+\.so\.6\]'
	check_not "no text relocations" "$R" 'TEXTREL'
	D=$(defines_runtime "$OUT/$lib")
	if [ -z "$D" ]; then pass "no C++ runtime of its own"; else fail "defines $D"; fi
	check_dynstr "$OUT/$lib"
	rm -f "${R:?}"
done

for prog in cxxso-main cxxso-main-static cxxso-cmain; do
	echo "== $prog"
	D=$(defines_runtime "$OUT/$prog")
	if [ "$prog" = cxxso-main-static ]; then
		if needed "$OUT/$prog" | grep -qx 'libstdc++.so.6'; then
			fail "does not NEED libstdc++.so.6 itself"
		else
			pass "does not NEED libstdc++.so.6 itself"
		fi
		# libstdc++.so.6's start-up check compares these with its own.
		X=$("$NM" -D --defined-only "$OUT/$prog" | awk '{print $3}' \
			| grep -cE '^(__gxx_personality_v0|__cxa_throw|__cxa_begin_catch)$' || true)
		if [ "$X" = 3 ]; then
			pass "exports its own __gxx_personality_v0, __cxa_throw and __cxa_begin_catch"
		else
			fail "exports its own __gxx_personality_v0, __cxa_throw and __cxa_begin_catch ($X of 3)"
		fi
	else
		if [ "$prog" = cxxso-main ]; then
			if needed "$OUT/$prog" | grep -qx 'libstdc++.so.6'; then
				pass "NEEDs libstdc++.so.6"
			else
				fail "NEEDs libstdc++.so.6"
			fi
		fi
		if [ "$prog" = cxxso-main ]; then
			if [ -z "$D" ]; then pass "no C++ runtime of its own"; else fail "defines $D"; fi
		else
			# ld fills libcxxsob's _Unwind_Resume from libgcc.a before it
			# reads libstdc++.so.6, so a C program gets a copy of the
			# unwinder; it is hidden, so nothing binds to it.
			X=$("$NM" -D --defined-only "$OUT/$prog" | awk '{print $3}' | grep -E "$RUNTIME_RE" || true)
			if [ -z "$X" ]; then pass "exports none of the C++ runtime"; else fail "exports $(echo $X)"; fi
		fi
	fi
	check_dynstr "$OUT/$prog"
	# Every symbol any object in the process leaves undefined is defined by
	# one of them, libc.so.1 included.
	OBJS="$OUT/$prog $OUT/libcxxsob.so.1 $OUT/libcxxsoa.so.1 $OUT/libstdc++.so.6 $OUT/libamixrt.so.1"
	DEFS="$OUT/$prog.defs"
	{ for o in $OBJS; do "$NM" -D --defined-only "$o"; done; } | awk '{print $3}' | cat - "$LIBC_SYMS" | sort -u > "$DEFS"
	MISSING=$(for o in $OBJS; do "$NM" -D --undefined-only "$o"; done | awk '$1 == "U" {print $2}' | sort -u | comm -23 - "$DEFS")
	if [ -z "$MISSING" ]; then pass "every undefined symbol is defined at run time"; else fail "left undefined: $(echo $MISSING)"; fi
	rm -f "${DEFS:?}"
done

echo "== g++ -shared -static-libstdc++ still copies the static runtime"
if "$CXX" -O2 -fPIC -shared -static-libstdc++ -o "$OUT/static-rt.so" probe/cxxso-a.cpp 2>&1 | grep -q TEXTREL \
   && ! needed "$OUT/static-rt.so" | grep -qx 'libstdc++.so.6'; then
	pass "a library with its own copy, and ld says so"
else
	fail "a library with its own copy, and ld says so"
fi
rm -f "${OUT:?}/static-rt.so"

echo "== static C++ programs are what they were"
for p in cxxprobe stl; do
	"$CXX" -O2 -o "$OUT/static/$p" "probe/$p.cpp"
	"$CXX" -O2 -static-libstdc++ -o "$OUT/static/$p.s" "probe/$p.cpp"
	if cmp -s "$OUT/static/$p" "$OUT/static/$p.s"; then
		pass "$p: the default link is the static one, byte for byte"
	else
		fail "$p: the default link is the static one, byte for byte"
	fi
	if needed "$OUT/static/$p" | grep -q 'libstdc++'; then
		fail "$p: NEEDs nothing of libstdc++"
	else
		pass "$p: NEEDs nothing of libstdc++"
	fi
	rm -f "${OUT:?}/static/$p.s"
	if [ -n "$CXX_STATIC_REF" ]; then
		if cmp -s "$OUT/static/$p" "$CXX_STATIC_REF/$p"; then
			pass "$p: the same bytes as $CXX_STATIC_REF/$p"
		else
			fail "$p: the same bytes as $CXX_STATIC_REF/$p"
		fi
	fi
done
rm -f "${LIBC_SYMS:?}" "${OUT:?}/rt.syms" "${OUT:?}/cxxrt.exports"

cat > "$OUT/run-cxx-shared.sh" <<'EOF'
# run-cxx-shared.sh -- run on AMIX, in the directory test-cxx-shared.sh staged.
LD_LIBRARY_PATH=`pwd`
export LD_LIBRARY_PATH
for p in cxxso-main cxxso-cmain cxxso-main-static; do
	echo "== $p"
	./$p
	echo "exit status: $?"
done
EOF

echo
if [ "$FAILED" = 0 ]; then
	echo "test-cxx-shared.sh: all checks passed; staged in $OUT for a run on AMIX"
else
	echo "test-cxx-shared.sh: FAILED"
	exit 1
fi
