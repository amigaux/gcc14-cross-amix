#!/bin/bash
# build-cxx-shared.sh -- build libstdc++.so.6, the shared C++ runtime, from
# GCC's build tree.  toolchain/build-cross.sh runs it; so does
# toolchain/rebuild-target-libs.sh after it remakes the static libraries.
#
# WHY
# ---
# A C++ library built with the static runtime carries its own copy of
# operator new, __cxa_throw, the personality routine and the unwinder.  The
# copy is not position-independent, so its pages turn private in every
# process (text relocations), and each library has its own exception globals
# and its own registry of .eh_frame sections, so an exception thrown in one
# library cannot be caught in another or in the program.  libstdc++.so.6 is
# one copy for the whole process: libstdc++ and libsupc++, with the unwinder
# from libgcc exported from it, which is what libgcc_s.so.1 holds elsewhere.
# docs/TOOLCHAIN.md, "The shared C++ runtime", has the measurements.
#
# GCC builds none of this for m68k-elf: to libtool the target has no shared
# libraries.  So the objects are made here, from GCC's own build directories,
# and linked by `gcc -shared` like any other library here:
#
#  1. libgcc, position-independent.  libgcc's build directory is copied to
#     libgcc-pic beside it (the same depth, so its relative paths hold) and
#     made in its shared-library mode: every member as the _s.o object that
#     libgcc_s would be made of, compiled with -fPIC and without the
#     -fvisibility=hidden of the static libgcc.a.  The _s.o objects already
#     in the copy are removed first: the static build makes some of them
#     too, without -fPIC, to generate its lists of hidden symbols.  The
#     unwinder objects are linked whole and exported; the rest go in through
#     an archive and are hidden (--exclude-libs), as in any library here.
#  2. libstdc++, position-independent.  libstdc++-v3-pic is configured from
#     the static build's own config.status and config.cache, so it has the
#     same configuration, and made with -fPIC -DPIC added to the static
#     build's flags.  No -D_GLIBCXX_SHARED: that adds only compatibility
#     symbols for old binaries, which need symbol versioning, and AMIX's
#     runtime linker has none.  So the shared library holds exactly the
#     static library's code, built position-independent.
#  3. math_stubs_long_double.o is kept out of libstdc++.so.6.  SVR4.0 has no
#     long double functions, and libstdc++ wraps the double ones in that
#     object: eighteen of them are in libm, which AMIX has only as a static,
#     non-PIC archive, so the library would get text relocations or every
#     program a copy of libm.  It goes into libstdc++_nonshared.a, which the
#     link-time libstdc++.so names after libstdc++.so.6.
#  4. compat/amix_cxxrt.c: the start-up check that the program does not
#     carry a second C++ runtime, and hidden copies of floor and frexpl,
#     which the hash tables use (floor is in libm; frexpl is one of the stubs
#     of step 3).
#
# Installed in $prefix/m68k-elf/lib/cxxrt, a directory of its own, so that a
# program linked the default way still finds libstdc++.a and is the same,
# byte for byte, as before.  The specs search it for `g++ -shared` and for
# `g++ -shared-libgcc` only.  On the machine libstdc++.so.6 goes in
# /usr/lib, beside libamixrt.so.1.
#
# Usage: toolchain/build-cxx-shared.sh
set -e
cd "$(dirname "$0")/.."
REPO="$(pwd)"
export LC_ALL=C
[ -f config.sh ] || { echo "build-cxx-shared.sh: no config.sh" >&2; exit 1; }
. ./config.sh
: "${TC_PREFIX:?}" "${AMIX_SYSROOT:?}"
TARGET="${TC_TARGET:-m68k-elf}"
WORK="${TC_WORK:-/tmp/m68k-amix-build}"
T="$WORK/build-gcc/$TARGET"
for f in "$T/libgcc/Makefile" "$T/libstdc++-v3/config.status" "$T/libstdc++-v3/config.cache"; do
	[ -f "$f" ] || {
		echo "build-cxx-shared.sh: no $f -- it builds from GCC's build tree;" >&2
		echo "  rm -rf $TC_PREFIX and run toolchain/build-cross.sh" >&2
		exit 1
	}
done
export PATH="$TC_PREFIX/bin:$PATH"
export MAKEINFO=true
JOBS=$(nproc)
CC="$TC_PREFIX/bin/$TARGET-gcc"
AR="$TC_PREFIX/bin/$TARGET-ar"
NM="$TC_PREFIX/bin/$TARGET-nm"
STRIP="$TC_PREFIX/bin/$TARGET-strip"
OUT="$WORK/cxxrt"
rm -rf "${OUT:?}"
mkdir -p "$OUT"

# The static build's flags, plus PIC.
flags_of() {  # $1 = Makefile, $2 = variable
	sed -n "s/^$2 = //p" "$1" | head -1
}
PIC="-fPIC -DPIC"
LIBGCC_CFLAGS="$(flags_of "$T/libgcc/Makefile" CFLAGS) $PIC"
STD_CFLAGS="$(flags_of "$T/libstdc++-v3/Makefile" CFLAGS) $PIC"
STD_CXXFLAGS="$(flags_of "$T/libstdc++-v3/Makefile" CXXFLAGS) $PIC"

echo "== 1/4 libgcc, position-independent ($LIBGCC_CFLAGS)"
rm -rf "${T:?}/libgcc-pic"
cp -a "$T/libgcc" "$T/libgcc-pic"
rm -f "$T"/libgcc-pic/*_s.o "$T"/libgcc-pic/*_s.dep
# enable_shared and SHLIB_LINK on the command line select libgcc's shared
# mode (its Makefile turns it off when SHLIB_LINK is empty); the extra rule
# names every object that mode makes.
make -C "$T/libgcc-pic" -f Makefile -f - -j"$JOBS" MAKEINFO=true \
	enable_shared=yes SHLIB_LINK=: CFLAGS="$LIBGCC_CFLAGS" amix-pic-objects \
	> "$OUT/libgcc-pic.log" 2>&1 <<'EOF' || { tail -20 "$OUT/libgcc-pic.log" >&2; exit 1; }
amix-pic-objects: $(libgcc-s-objects)
	echo $(sort $(libgcc-s-objects)) > $@
EOF
EH="unwind-dw2_s.o unwind-dw2-fde_s.o unwind-sjlj_s.o unwind-c_s.o emutls_s.o"
EH_OBJS=""
for o in $EH; do
	grep -qw "$o" "$T/libgcc-pic/amix-pic-objects" || { echo "build-cxx-shared.sh: libgcc made no $o" >&2; exit 1; }
	EH_OBJS="$EH_OBJS $T/libgcc-pic/$o"
done
REST=""
for o in $(cat "$T/libgcc-pic/amix-pic-objects"); do
	case " $EH " in *" $o "*) ;; *) REST="$REST $T/libgcc-pic/$o" ;; esac
done
$AR rcs "$OUT/libgcc_pic.a" $REST

echo "== 2/4 libstdc++, position-independent ($STD_CXXFLAGS)"
P="$T/libstdc++-v3-pic"
rm -rf "${P:?}"
mkdir -p "$P"
cp "$T/libstdc++-v3/config.status" "$T/libstdc++-v3/config.cache" "$P/"
( cd "$P" && ./config.status --recheck && ./config.status ) > "$OUT/libstdc++-configure.log" 2>&1 || {
	tail -20 "$OUT/libstdc++-configure.log" >&2; exit 1; }
cmp -s "$T/libstdc++-v3/config.h" "$P/config.h" || {
	echo "build-cxx-shared.sh: $P/config.h differs from the static build's" >&2; exit 1; }
make -C "$P" -j"$JOBS" MAKEINFO=true CFLAGS="$STD_CFLAGS" CXXFLAGS="$STD_CXXFLAGS" \
	> "$OUT/libstdc++-pic.log" 2>&1 || { tail -30 "$OUT/libstdc++-pic.log" >&2; exit 1; }
STD_A="$P/src/.libs/libstdc++.a"
DUP=$($AR t "$STD_A" | sort | uniq -d)
[ -z "$DUP" ] || { echo "build-cxx-shared.sh: members named twice in $STD_A: $DUP" >&2; exit 1; }
STUBS=math_stubs_long_double.o
$AR t "$STD_A" | grep -qx "$STUBS" || { echo "build-cxx-shared.sh: no $STUBS in $STD_A" >&2; exit 1; }
cp "$STD_A" "$OUT/libstdc++-shared.a"
$AR d "$OUT/libstdc++-shared.a" "$STUBS"
( cd "$OUT" && $AR x "$STD_A" "$STUBS" && $AR rcs libstdc++_nonshared.a "$STUBS" && rm -f "$STUBS" )

echo "== 3/4 linking libstdc++.so.6"
$CC -O2 -fPIC -Wall -c -o "$OUT/amix_cxxrt.o" compat/amix_cxxrt.c
# This library's own definitions under a second, hidden name, for the check
# in amix_cxxrt.c.
cat > "$OUT/cxxrt-own.ld" <<'EOF'
HIDDEN(__amix_cxxrt_own_personality = __gxx_personality_v0);
HIDDEN(__amix_cxxrt_own_throw = __cxa_throw);
HIDDEN(__amix_cxxrt_own_begin_catch = __cxa_begin_catch);
EOF
# -z text: a text relocation is an error, not a warning.
$CC -shared -Wl,-soname,libstdc++.so.6 -Wl,-z,text -o "$OUT/libstdc++.so.6" \
	"$OUT/cxxrt-own.ld" \
	-Wl,--whole-archive "$OUT/libstdc++-shared.a" $EH_OBJS -Wl,--no-whole-archive \
	"$OUT/amix_cxxrt.o" "$OUT/libgcc_pic.a" -Wl,--exclude-libs,libgcc_pic.a

echo "== 4/4 checks"
# Every symbol it leaves undefined is one libc.so.1 or libamixrt.so.1
# exports: there is nothing else for the runtime linker to find it in.
RTDIR=$("$CC" -print-file-name=amixrt)
$NM --defined-only "$AMIX_SYSROOT/usr/lib/libc.so.1" | awk '$2 ~ /^[A-Z]$/ {print $3}' > "$OUT/defined.syms"
$NM -D --defined-only "$RTDIR/libamixrt.so.1" | awk '{print $3}' >> "$OUT/defined.syms"
sort -u -o "$OUT/defined.syms" "$OUT/defined.syms"
MISSING=$($NM -D --undefined-only "$OUT/libstdc++.so.6" | awk '$1 == "U" {print $2}' | sort -u \
	| comm -23 - "$OUT/defined.syms")
[ -z "$MISSING" ] || { echo "build-cxx-shared.sh: undefined and in neither libc.so.1 nor libamixrt.so.1: $(echo $MISSING)" >&2; exit 1; }
# A GOT-offset relocation with an addend links to the wrong address: see
# tools/check-got-addend.py.  Only objects can be checked.
python3 tools/check-got-addend.py "$OUT/libstdc++-shared.a" "$OUT/libstdc++_nonshared.a" \
	$EH_OBJS "$OUT/libgcc_pic.a" "$OUT/amix_cxxrt.o" > "$OUT/got-addend.log" || {
	cat "$OUT/got-addend.log" >&2; exit 1; }
tail -1 "$OUT/got-addend.log"

D="$TC_PREFIX/$TARGET/lib/cxxrt"
rm -rf "${D:?}"
mkdir -p "$D"
# Stripped, as libamixrt.so.1 is: --strip-unneeded keeps .dynsym and what
# the runtime linker reads, and drops the DWARF 5 that AMIX's sdb cannot
# read.  The unstripped copy stays in $OUT.
"$STRIP" --strip-unneeded -o "$D/libstdc++.so.6" "$OUT/libstdc++.so.6"
cp "$OUT/libstdc++_nonshared.a" "$D/"
# What -lstdc++ finds here at link time.  ld looks for both names along the
# library path, which has this directory on it whenever this file is read.
cat > "$D/libstdc++.so" <<'EOF'
/* GNU ld script: the shared C++ runtime (libstdc++.so.6), and the stubs
   it leaves out (libstdc++_nonshared.a).  toolchain/build-cxx-shared.sh */
INPUT ( libstdc++.so.6 libstdc++_nonshared.a )
EOF
echo "build-cxx-shared.sh: installed $D/libstdc++.so.6"
