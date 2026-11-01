#!/bin/bash
# test-shared.sh -- build shared libraries with `gcc -shared` and check they
# have the shape AMIX's runtime linker loads.
#
# Builds probe/solib-c.c (a C library with a constructor and a destructor) and
# probe/solib-cxx.cpp (a C++ library with a static object) with
# `gcc -shared -fPIC`, links probe/solib-main.c and probe/solib-main-cxx.cpp
# against them the ordinary way, and checks every file with readelf.  Nothing
# here runs on the host: the result is staged in build/test-shared/, with
# run-shared.sh, for a run on AMIX:
#
#   copy build/test-shared/ to the machine, then there:  sh run-shared.sh
#
# Expected there, for each program: each library constructor's two lines,
# "(write)" then "(stdio)", before main's line; main's line with env=1 and
# ending in PASS; the destructor lines after it; "exit status: 0".  A
# "(write)" line without its "(stdio)" line, or env=0, means the constructor
# ran from the runtime linker's DT_INIT, before libc was consistent with the
# program.  No constructor line at all means .init_array was not walked.  The
# C++ destructors come first, newest first, from libamixc99's exit hook;
# solib-c's C destructor comes last, from its DT_FINI.
#
# The programs are linked with g++ because the C++ library uses the C++
# runtime, and a shared library leaves that to the executable.
set -e
cd "$(dirname "$0")/.."
# sort and comm have to agree on the order.
export LC_ALL=C
[ -f config.sh ] || { echo "test-shared.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
CXX="$TC_PREFIX/bin/$TC_TARGET-g++"
READELF="$TC_PREFIX/bin/$TC_TARGET-readelf"
OUT=build/test-shared
rm -rf "${OUT:?}"
mkdir -p "$OUT"
# libamixrt.so.1, which the libraries NEED for snprintf and __cxa_atexit, is
# staged with them, so the run on AMIX finds it by LD_LIBRARY_PATH.
RTDIR=$("$CC" -print-file-name=amixrt)
[ -f "$RTDIR/libamixrt.so.1" ] || { echo "test-shared.sh: no libamixrt.so.1 in $RTDIR -- run tools/build-compat.sh" >&2; exit 1; }
cp "$RTDIR/libamixrt.so.1" "$OUT/"

FAILED=0
pass() { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; FAILED=1; }
check() {  # $1 = what, $2 = file holding readelf output, $3 = pattern that must match
	if grep -qE -- "$3" "$2"; then pass "$1"; else fail "$1"; fi
}
check_not() {  # $1 = what, $2 = file, $3 = pattern that must not match
	if grep -qE -- "$3" "$2"; then fail "$1"; else pass "$1"; fi
}

echo "== building"
"$CC" -O2 -fPIC -shared -Wl,-soname,libsolibc.so.1 -o "$OUT/libsolibc.so.1" probe/solib-c.c
"$CC" -O2 -fPIC -shared -Wl,-soname,libsolibcxx.so.1 -o "$OUT/libsolibcxx.so.1" probe/solib-cxx.cpp
"$CC" -O2 -c -o "$OUT/solib-main.o" probe/solib-main.c
"$CXX" -O2 -o "$OUT/solib-main" "$OUT/solib-main.o" "$OUT/libsolibc.so.1" "$OUT/libsolibcxx.so.1"
"$CXX" -O2 -o "$OUT/solib-main-cxx" probe/solib-main-cxx.cpp "$OUT/libsolibcxx.so.1"
rm -f "$OUT/solib-main.o"

for lib in libsolibc.so.1 libsolibcxx.so.1; do
	echo "== $lib"
	R="$OUT/$lib.readelf"
	"$READELF" -hldW --dyn-syms "$OUT/$lib" > "$R"
	check "a shared object (ET_DYN)" "$R" 'Type: +DYN'
	check "SONAME $lib" "$R" "\(SONAME\) +Library soname: \[$lib\]"
	check_not "no program interpreter" "$R" 'INTERP|Requesting program interpreter'
	check "DT_INIT, which AMIX's runtime linker calls" "$R" '\(INIT\) '
	check "DT_FINI, which it calls at exit" "$R" '\(FINI\) '
	check_not "no text relocations" "$R" 'TEXTREL'
	# Both use something libc.so.1 lacks: snprintf, and __cxa_atexit.
	check "NEEDED libamixrt.so.1, for what libc.so.1 lacks" "$R" '\(NEEDED\) +Shared library: \[libamixrt\.so\.1\]'
	if grep -E '\(NEEDED\)' "$R" | grep -qv '\[libamixrt\.so\.1\]'; then
		fail "no other NEEDED, and never libc.so.1"
	else
		pass "no other NEEDED, and never libc.so.1"
	fi
	check_not "the startup code exports nothing" "$R" \
		' [0-9]+ (__amix_[a-z_]+|__cxa_finalize|__dso_handle|frame_dummy|__divdi3)$'
	# A constructor makes .init_array 8 bytes: amix_initS.o's null entry and
	# the constructor's.
	check "one constructor in .init_array" "$R" '\(INIT_ARRAYSZ\) +8 \(bytes\)'
	rm -f "$R"
done

# What the runtime linker will search for a library's undefined symbols: the
# program's exports, libc.so.1 (whose symbol table is its dynamic one, so plain
# nm reads it) and the other libraries.
NM="$TC_PREFIX/bin/$TC_TARGET-nm"
LIBC_SYMS="$OUT/libc.so.1.syms"
"$NM" --defined-only "$AMIX_SYSROOT/usr/lib/libc.so.1" | awk '$2 ~ /^[A-Z]$/ {print $3}' > "$LIBC_SYMS"

for prog in solib-main solib-main-cxx; do
	echo "== $prog"
	R="$OUT/$prog.readelf"
	"$READELF" -hldW "$OUT/$prog" > "$R"
	check "an executable (ET_EXEC)" "$R" 'Type: +EXEC'
	check "interpreter /usr/lib/libc.so.1" "$R" 'Requesting program interpreter: /usr/lib/libc.so.1'
	check "NEEDED libsolibcxx.so.1" "$R" '\(NEEDED\) +Shared library: \[libsolibcxx.so.1\]'
	check_not "the program does not NEED libamixrt.so.1: it links its own copies" "$R" \
		'\(NEEDED\) +Shared library: \[libamixrt\.so\.1\]'
	LIBS="$(sed -nE 's/.*\(NEEDED\) +Shared library: \[(libsolib[a-z]*\.so\.1)\]/\1/p' "$R") libamixrt.so.1"
	DEFS="$OUT/$prog.defs"
	{ "$NM" -D --defined-only "$OUT/$prog"; for l in $LIBS; do "$NM" -D --defined-only "$OUT/$l"; done; } \
		| awk '{print $3}' | cat - "$LIBC_SYMS" | sort -u > "$DEFS"
	MISSING=$(for l in $LIBS; do "$NM" -D --undefined-only "$OUT/$l"; done \
		| awk '$1 == "U" {print $2}' | sort -u | comm -23 - "$DEFS")
	if [ -z "$MISSING" ]; then
		pass "every undefined symbol of $(echo $LIBS) is defined at run time"
	else
		fail "left undefined at run time: $(echo $MISSING)"
	fi
	rm -f "$R" "$DEFS"
done
rm -f "$LIBC_SYMS"

echo "== an object built without -fPIC is reported"
if "$CC" -O2 -shared -o "$OUT/nopic.so" probe/solib-c.c 2>&1 | grep -q TEXTREL; then
	pass "ld warns about the text relocations"
else
	fail "ld warns about the text relocations"
fi
rm -f "$OUT/nopic.so"

cat > "$OUT/run-shared.sh" <<'EOF'
# run-shared.sh -- run on AMIX, in the directory test-shared.sh staged.
LD_LIBRARY_PATH=`pwd`
export LD_LIBRARY_PATH
for p in solib-main solib-main-cxx; do
	echo "== $p"
	./$p
	echo "exit status: $?"
done
EOF

echo
if [ "$FAILED" = 0 ]; then
	echo "test-shared.sh: all checks passed; staged in $OUT for a run on AMIX"
else
	echo "test-shared.sh: FAILED"
	exit 1
fi
