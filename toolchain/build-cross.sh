#!/bin/bash
# build-cross.sh -- build a modern C and C++ cross compiler for AMIX.
#
# WHY THIS EXISTS
# ---------------
# AMIX's own toolchain is gcc 2.7.2.3: no C++ at all, no -m68060, no C99.  The
# port needs C++03, so a compiler has to be built either way -- and the same
# build hands us a modern C compiler for free.
#
# WHY --target=m68k-elf AND NOT A NEW m68k-amix TARGET
# ----------------------------------------------------
# GCC dropped every m68k SVR4 target years ago (GCC 14 has elf, netbsdelf,
# uclinux, linux, rtems and nothing else), so an "m68k-amix" triple would need a
# config.gcc patch we would then carry forever.  m68k-elf is close enough that
# one header covers the difference: it returns values the embedded way, and
# toolchain/amix-abi.h gives it AMIX's SVR4 convention instead -- pointers in
# %a0, structs in memory.  That header is appended to m68k-elf's tm_file below;
# everything else AMIX-specific lives in the specs and tools/amix-cc.sh.
#
# WHY m68k-elf RATHER THAN THE PACKAGED m68k-linux-gnu COMPILER
# -------------------------------------------------------------
# Debian's m68k-linux-gnu g++ cannot give us working C++ exceptions here: its
# libgcc_eh.a resolves _dl_find_object and the whole pthread_* family, none of
# which AMIX has.  Those come from the glibc-flavoured unwinder, which locates
# exception tables by asking the dynamic loader.  An elf target instead builds
# the registry unwinder, where crtbegin registers .eh_frame from .init at
# startup and no loader support is needed -- and --disable-threads turns the
# pthread locking into no-ops.  That is the whole reason to build our own.
#
# Usage:  toolchain/build-cross.sh [--jobs N]
# Reads AMIX_SYSROOT from config.sh; installs into $TC_PREFIX (see below).
set -e
cd "$(dirname "$0")/.."
REPO="$(pwd)"
[ -f config.sh ] || { echo "build-cross.sh: no config.sh" >&2; exit 1; }
. ./config.sh
: "${AMIX_SYSROOT:?set AMIX_SYSROOT in config.sh}"
[ -d "$AMIX_SYSROOT/usr/include" ] || { echo "build-cross.sh: no headers in $AMIX_SYSROOT/usr/include" >&2; exit 1; }

TARGET=m68k-elf
TC_PREFIX="${TC_PREFIX:-$HOME/opt/m68k-amix}"
# The build tree is large and disposable; keep it off the home filesystem.
WORK="${TC_WORK:-/tmp/m68k-amix-build}"
JOBS=$(nproc)
# Neither build needs its manuals, and texinfo is not installed here: without
# this, binutils dies in doc/bfd.info with "makeinfo: not found".
export MAKEINFO=true
[ "$1" = "--jobs" ] && JOBS="$2"

BINUTILS_VER=2.43.1
GCC_VER=14.2.0

mkdir -p "$WORK/src" "$TC_PREFIX"
export PATH="$TC_PREFIX/bin:$PATH"

fetch() {  # $1 = url, $2 = filename
	[ -f "$WORK/src/$2" ] && return 0
	echo "== fetching $2"
	curl -fL --retry 3 -o "$WORK/src/$2.part" "$1"
	mv "$WORK/src/$2.part" "$WORK/src/$2"
}

# ---------------------------------------------------------------- binutils ---
if [ ! -x "$TC_PREFIX/bin/$TARGET-ld" ]; then
	fetch "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz" "binutils-$BINUTILS_VER.tar.xz"
	rm -rf "$WORK/binutils-$BINUTILS_VER" "$WORK/build-binutils"
	tar -C "$WORK" -xf "$WORK/src/binutils-$BINUTILS_VER.tar.xz"
	mkdir -p "$WORK/build-binutils"
	cd "$WORK/build-binutils"
	"$WORK/binutils-$BINUTILS_VER/configure" \
		--target="$TARGET" --prefix="$TC_PREFIX" \
		--with-sysroot="$AMIX_SYSROOT" \
		--disable-nls --disable-werror --disable-gdb --disable-sim \
		--disable-libdecnumber --disable-readline
	make -j"$JOBS" MAKEINFO=true
	make install MAKEINFO=true
	cd "$WORK"
	# Reclaim the space before GCC needs it.
	rm -rf "$WORK/build-binutils" "$WORK/binutils-$BINUTILS_VER"
	echo "== binutils installed"
fi

# --------------------------------------------------------------------- gcc ---
# Done means libstdc++ is installed, not that g++ is: install-gcc puts g++ in
# place long before libstdc++ is built, so a run that failed there left a g++
# behind, and testing for it made the next run skip everything and report
# success with no C++ library.
#
# Done also means built with the current toolchain/amix-abi.h.  A compiler from
# before that header returns pointers in %d0 only, so it is rebuilt rather
# than kept: its libraries are wrong for every caller the new one compiles.
ABI_STAMP="$TC_PREFIX/.amix-abi.sha256"
ABI_SUM=$(sha256sum "$REPO/toolchain/amix-abi.h" | cut -d' ' -f1)
if [ ! -f "$TC_PREFIX/$TARGET/lib/libstdc++.a" ] || [ "$(cat "$ABI_STAMP" 2>/dev/null)" != "$ABI_SUM" ]; then
	fetch "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz" "gcc-$GCC_VER.tar.xz"
	if [ ! -d "$WORK/gcc-$GCC_VER" ]; then
		tar -C "$WORK" -xf "$WORK/src/gcc-$GCC_VER.tar.xz"
		cd "$WORK/gcc-$GCC_VER"
		# mpfr and mpc are not installed on this host; --no-graphite skips isl,
		# which is only needed for loop optimisations we do not use.
		./contrib/download_prerequisites --no-graphite
	fi
	# AMIX's return convention, which m68k-elf does not have: see the header.
	# Appended to the m68k-*-elf* tm_file -- the one line in config.gcc that
	# ends in m68020-elf.h and a quote -- so it is read after m68kemb.h, whose
	# DEFAULT_PCC_STRUCT_RETURN it replaces.  Idempotent.
	cp "$REPO/toolchain/amix-abi.h" "$WORK/gcc-$GCC_VER/gcc/config/m68k/amix-abi.h"
	sed -i '/^\ttm_file=.*m68k\/m68kemb\.h m68k\/m68020-elf\.h"$/s|"$| m68k/amix-abi.h"|' \
		"$WORK/gcc-$GCC_VER/gcc/config.gcc"
	grep -q 'm68020-elf.h m68k/amix-abi.h"' "$WORK/gcc-$GCC_VER/gcc/config.gcc" || {
		echo "build-cross.sh: could not add amix-abi.h to config.gcc" >&2; exit 1; }
	python3 "$REPO/toolchain/patch-libstdcxx-configure.py" \
		"$WORK/gcc-$GCC_VER/libstdc++-v3/configure" "$AMIX_SYSROOT"

	rm -rf "$WORK/build-gcc"; mkdir -p "$WORK/build-gcc"
	cd "$WORK/build-gcc"
	# --disable-gcov: libgcov wants MAP_ANONYMOUS and intptr_t, and SVR4.0 has
	#   neither.  Coverage instrumentation is not something this port needs, and
	#   without this the libgcc build stops in _gcov_merge_topn.o.
	# --disable-multilib: the m68k-elf target otherwise builds ~15 multilibs,
	#   almost all of them ColdFire.  Each one would get its own libgcc and its
	#   own libstdc++; on a host with a few gigabytes free that fills the disk
	#   before the build finishes.  One library serves the whole 68020..68060
	#   range, which is every machine this port targets.
	# --with-cpu=m68020-60: NOT a performance choice, a correctness one, in two
	#   directions.  libgcc's __udivmoddi4 compiled for a plain 68020 uses
	#   divul / divull / mulul -- the 32x64 divide and 64-bit multiply forms the
	#   68060 REMOVED -- and with no ISP handler in the kernel the process dies
	#   with SIGKILL.  Measured: any 64-bit divide by a NON-CONSTANT killed it,
	#   while dividing by a constant was fine because GCC folds that into a
	#   multiply and never calls libgcc.  Tuning for the 68060 avoids them.
	#   But --with-cpu=68060 also makes the 68060 the ARCHITECTURE, and then
	#   GCC uses the 68040/060-only FPU forms (fdmove, fsadd, fdmul...), which a
	#   68881/68882 does not have: libgcc.a had 491 of them, libstdc++.a 123, and
	#   on a 68030 + 68882 the first one kills the process with SIGSYS.  The
	#   m68020-60 form is the 68020 instruction set and 68881 FPU tuned for the
	#   whole range, so the runtime runs on every one of them.  (The "m" is
	#   required: config.gcc only knows this name with it.)  -m68060 remains
	#   available for code that will only ever run on a 68060.
	#   This has to be the compiler DEFAULT: passing CFLAGS_FOR_TARGET does not
	#   reach libgcc's own compile (measured -- the instructions survived).
	# libstdc++ is built HOSTED -- containers, strings, the lot -- which is why
	#   patch-libstdcxx-configure.py above has to tell its configure what the
	#   AMIX C library declares.  See docs/TOOLCHAIN.md section 13.
	"$WORK/gcc-$GCC_VER/configure" \
		--target="$TARGET" --prefix="$TC_PREFIX" \
		--with-sysroot="$AMIX_SYSROOT" \
		--enable-languages=c,c++ \
		--disable-nls --disable-shared --disable-threads \
		--disable-libssp --disable-libgomp --disable-libatomic \
		--disable-libquadmath --disable-libvtv --disable-libsanitizer \
		--disable-libstdcxx-filesystem-ts --disable-libstdcxx-pch \
		--disable-libstdcxx-backtrace \
		--disable-multilib --disable-gcov \
		--with-cpu=m68020-60
	make -j"$JOBS" MAKEINFO=true all-gcc
	make install-gcc MAKEINFO=true

	# The specs are what make this compiler able to link at all; see
	# toolchain/install-specs.sh.  BOTH drivers need them: the installed one for
	# our own use, and the build tree's xgcc for libstdc++'s configure link
	# tests -- without which it sets GCC_NO_EXECUTABLES and later aborts with
	# "Link tests are not allowed after GCC_NO_EXECUTABLES".
	"$REPO/toolchain/install-specs.sh" >/dev/null
	GCCVER=$("$TC_PREFIX/bin/$TARGET-gcc" -dumpversion)
	cp "$TC_PREFIX/lib/gcc/$TARGET/$GCCVER/specs" "$WORK/build-gcc/gcc/specs"
	# gcc/Makefile lists specs in GCC_PASSES, which fixincludes depends on, so
	# the copy above makes fixincludes out of date -- and it starts with
	# `rm -rf include-fixed`.  Let it run again NOW, before the repaired headers
	# are copied in.  Otherwise it runs inside all-target-libgcc (configuring
	# libgcc depends on all-gcc), silently wipes the copy below, and libstdc++
	# dies much later on the raw K&R <sys/stat.h> -- measured on a build from
	# zero; a build tree that already had them installed hides it.
	make -j"$JOBS" MAKEINFO=true all-gcc

	# Repaired system headers, for the same two drivers and the same reason:
	# libstdc++'s own sources need the C99 macros AMIX's <math.h> lacks.
	"$REPO/toolchain/install-fixed-headers.sh"
	mkdir -p "$WORK/build-gcc/gcc/include-fixed"
	cp -R "$TC_PREFIX/lib/gcc/$TARGET/$GCCVER/include-fixed/." \
	      "$WORK/build-gcc/gcc/include-fixed/"

	# The compat layer, before anything tries to link.  The specs name
	# amix_init.o as a startfile and -lamixc99 in the lib spec, so EVERY link
	# needs both present -- including libstdc++'s configure link tests, which
	# abort the build when they fail.  It needs the compiler that was just
	# installed, which is why it cannot run earlier.
	"$REPO/tools/build-compat.sh"

	make -j"$JOBS" all-target-libgcc
	make install-target-libgcc
	make -j"$JOBS" all-target-libstdc++-v3 || {
		echo "!! libstdc++/libsupc++ failed -- C and libgcc are installed and usable."
		echo "!! See the log above for the first missing declaration."
		exit 1
	}
	make install-target-libstdc++-v3
	echo "$ABI_SUM" > "$ABI_STAMP"
	echo "== gcc installed"
fi

# The runtime is linked into every program, so it has to run on every CPU in
# the range: no 64-bit multiply/divide for the 68060, no 68040/060-only FPU
# forms for the 68881/68882.  Checked on every run, not only after a build: a
# prefix built by an earlier version of this script, --with-cpu=68060, passes
# the test above (libstdc++.a present, amix-abi.h unchanged) and would
# otherwise be kept.
GCCLIB=$(dirname "$("$TC_PREFIX/bin/$TARGET-gcc" -print-libgcc-file-name)")
RUNTIME=("$GCCLIB"/libgcc.a "$GCCLIB"/crt*.o "$TC_PREFIX/$TARGET/lib"/lib{stdc++,stdc++exp,supc++,amixc99}.a "$TC_PREFIX/$TARGET/lib/amix_init.o")
python3 "$REPO/tools/check-68060-safe.py" --68881 "${RUNTIME[@]}" || {
	echo "!! the runtime above is not safe on every 68020..68060 + 68881 machine." >&2
	echo "!! If it was built by an older version of this script, rebuild it:" >&2
	echo "!!   rm -rf $TC_PREFIX && toolchain/build-cross.sh" >&2
	exit 1
}

echo
echo "toolchain: $TC_PREFIX/bin/$TARGET-{gcc,g++}"
"$TC_PREFIX/bin/$TARGET-gcc" --version | head -1
echo "build tree left in $WORK -- safe to delete: rm -rf $WORK"
