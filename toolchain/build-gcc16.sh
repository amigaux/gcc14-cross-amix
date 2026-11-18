#!/bin/bash
# build-gcc16.sh -- build the GCC 16 cross compiler for AMIX kernel space.
#
# WHAT THIS IS
# ------------
# A second compiler beside the GCC 14 one that toolchain/build-cross.sh
# builds.  It is for the kernel: C only, freestanding, no C library, no
# libstdc++, no shared-library runtime.  It builds the same target form as
# GCC 14 -- m68k-elf, with toolchain/amix-abi.h for AMIX's return convention
# and the same compile-side specs (-malign-int, -mno-strict-align, the
# __amix__ macros) -- so that the two compilers can build one tree and be
# compared.  Nothing here touches the GCC 14 build, its sources or $TC_PREFIX.
#
# WHICH GCC 16
# ------------
# The GNU release gcc-16.2.0, signature-checked once and pinned by sha256
# below, plus the m68k back-end fixes in toolchain/patches-gcc16/: seven
# from the AmigaPorts amiga16.2 branch, one from GCC master, one generic
# pass fix found on that branch, and one of our own (0010, the memory
# bit-field offset guard that goes with 0008).  docs/GCC16.md says why this and not the
# AmigaPorts branch itself (which does not link a cc1 for m68k-elf), and
# which of the branch's changes were taken, left out, and why.
#
# binutils 2.46.1, also a signed GNU release, unpatched: the PIC relocation
# patch the GCC 14 binutils carry is for shared libraries, which the kernel
# has none of.  Its gas assembles the 68030, 68040 and 68060 supervisor
# instructions (pmove, ptest, pflush, movec, cinv, cpush, plpa) under -m68030,
# -m68040 and -m68060; the test set in tests/kernel-codegen/ checks that.
#
# --with-cpu=m68020-60 for the same reason as GCC 14 (docs/TOOLCHAIN.md,
# section 9): the 68020 instruction set without the 64-bit MULU.L/DIVU.L forms
# the 68060 lacks, and the 68881 FPU rather than the 68040/060-only FPU forms.
#
# Usage:  toolchain/build-gcc16.sh [--jobs N]
# Installs into $GCC16_PREFIX (default build/gcc16/prefix), builds in
# $GCC16_WORK (default build/gcc16/work) and keeps the downloads in
# build/gcc16/src.  config.sh is not needed; if it exists and sets GCC16_PREFIX
# or GCC16_WORK, those are used.  A second run with nothing changed does
# nothing; a change to a patch, to amix-abi.h or to the pins rebuilds GCC.
set -e
cd "$(dirname "$0")/.."
REPO="$(pwd)"
[ -f config.sh ] && . ./config.sh

TARGET=m68k-elf
PREFIX="${GCC16_PREFIX:-$REPO/build/gcc16/prefix}"
WORK="${GCC16_WORK:-$REPO/build/gcc16/work}"
SRC="$REPO/build/gcc16/src"
JOBS=$(nproc)
[ "$1" = "--jobs" ] && JOBS="$2"
# Neither build needs its manuals, and texinfo may not be installed.
export MAKEINFO=true

# The pins.  The sums were taken from tarballs whose GNU signatures checked
# out with gpgv against https://ftp.gnu.org/gnu/gnu-keyring.gpg (2026-11-17).
# GCC's own prerequisites (gmp, mpfr, mpc, gettext) are fetched by its
# contrib/download_prerequisites, which checks them against the sha512 list in
# the signed GCC tarball -- so they are pinned through it.
BINUTILS_VER=2.46.1
BINUTILS_SHA256=e127a709cba24c76de8936cb7083dd768f28cd37eb010492e2f19b71eb1294e4
GCC_VER=16.2.0
GCC_SHA256=e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e

mkdir -p "$SRC" "$WORK" "$PREFIX"
export PATH="$PREFIX/bin:$PATH"

fetch() {  # $1 = url, $2 = filename, $3 = sha256
	if [ ! -f "$SRC/$2" ]; then
		echo "== fetching $2"
		curl -fL --retry 3 -o "$SRC/$2.part" "$1"
		mv "$SRC/$2.part" "$SRC/$2"
	fi
	echo "$3  $SRC/$2" | sha256sum -c --quiet - || {
		echo "build-gcc16.sh: $2 does not match its pinned sha256 -- not using it" >&2
		exit 1
	}
}

# ---------------------------------------------------------------- binutils ---
BINUTILS_STAMP="$PREFIX/.binutils.stamp"
if [ ! -x "$PREFIX/bin/$TARGET-as" ] || [ "$(cat "$BINUTILS_STAMP" 2>/dev/null)" != "$BINUTILS_SHA256" ]; then
	fetch "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz" \
		"binutils-$BINUTILS_VER.tar.xz" "$BINUTILS_SHA256"
	rm -rf "${WORK:?}/binutils-$BINUTILS_VER" "${WORK:?}/build-binutils"
	tar -C "$WORK" -xf "$SRC/binutils-$BINUTILS_VER.tar.xz"
	mkdir -p "$WORK/build-binutils"
	cd "$WORK/build-binutils"
	"$WORK/binutils-$BINUTILS_VER/configure" \
		--target="$TARGET" --prefix="$PREFIX" \
		--disable-nls --disable-werror --disable-gdb --disable-gdbserver \
		--disable-sim --disable-libdecnumber --disable-readline --disable-gprofng
	make -j"$JOBS" MAKEINFO=true
	make install MAKEINFO=true
	cd "$REPO"
	rm -rf "${WORK:?}/build-binutils" "${WORK:?}/binutils-$BINUTILS_VER"
	echo "$BINUTILS_SHA256" > "$BINUTILS_STAMP"
	echo "== binutils $BINUTILS_VER installed"
fi

# --------------------------------------------------------------------- gcc ---
# Done means libgcc is installed and was built from these exact inputs: the
# tarball, the patches, amix-abi.h and this script's configure line.
GCC_STAMP="$PREFIX/.gcc.stamp"
GCC_SUM=$( { echo "$GCC_SHA256"; cat "$REPO"/toolchain/patches-gcc16/*.patch \
	"$REPO/toolchain/amix-abi.h"; sed -n '/^GCC_CONFIGURE=(/,/^)/p' "$REPO/toolchain/build-gcc16.sh"; } \
	| sha256sum | cut -d' ' -f1)
# --without-headers: there is no C library; libgcc is built inhibit_libc,
#   which is what a kernel links (the 64-bit multiply, divide and shift helpers).
# --disable-multilib, --disable-gcov, --disable-threads, --disable-shared: as
#   for GCC 14 (see toolchain/build-cross.sh), and nothing here needs them.
# --with-pkgversion names the build, so a kernel's .comment says which one.
GCC_CONFIGURE=(
	--target="$TARGET" --prefix="$PREFIX"
	--enable-languages=c
	--without-headers
	--disable-nls --disable-shared --disable-threads
	--disable-libssp --disable-libgomp --disable-libatomic
	--disable-libquadmath --disable-libsanitizer
	--disable-multilib --disable-gcov
	--with-cpu=m68020-60
	--with-as="$PREFIX/bin/$TARGET-as" --with-ld="$PREFIX/bin/$TARGET-ld"
	--with-pkgversion="gcc14-cross-amix GCC 16 kernel"
)
if [ ! -f "$PREFIX/lib/gcc/$TARGET/$GCC_VER/libgcc.a" ] || [ "$(cat "$GCC_STAMP" 2>/dev/null)" != "$GCC_SUM" ]; then
	fetch "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz" \
		"gcc-$GCC_VER.tar.xz" "$GCC_SHA256"
	# Always a fresh tree: patches are applied to the release, never to a tree
	# an earlier run already changed.
	rm -rf "${WORK:?}/gcc-$GCC_VER" "${WORK:?}/build-gcc"
	tar -C "$WORK" -xf "$SRC/gcc-$GCC_VER.tar.xz"
	for p in "$REPO"/toolchain/patches-gcc16/*.patch; do
		echo "== applying $(basename "$p")"
		patch -d "$WORK/gcc-$GCC_VER" -p1 --forward --no-backup-if-mismatch < "$p"
	done
	# AMIX's return convention, exactly as build-cross.sh adds it for GCC 14:
	# appended to the m68k-*-elf* tm_file, after m68kemb.h.
	cp "$REPO/toolchain/amix-abi.h" "$WORK/gcc-$GCC_VER/gcc/config/m68k/amix-abi.h"
	sed -i '/^\ttm_file=.*m68k\/m68kemb\.h m68k\/m68020-elf\.h"$/s|"$| m68k/amix-abi.h"|' \
		"$WORK/gcc-$GCC_VER/gcc/config.gcc"
	grep -q 'm68020-elf.h m68k/amix-abi.h"' "$WORK/gcc-$GCC_VER/gcc/config.gcc" || {
		echo "build-gcc16.sh: could not add amix-abi.h to config.gcc" >&2; exit 1; }
	# Their downloads go to the source tree, which is rebuilt each time; keep a
	# copy of the archives beside the other downloads so a rebuild is offline.
	cp -n "$SRC"/prereq/* "$WORK/gcc-$GCC_VER/" 2>/dev/null || true
	( cd "$WORK/gcc-$GCC_VER" && ./contrib/download_prerequisites --no-graphite )
	mkdir -p "$SRC/prereq"
	cp -n "$WORK/gcc-$GCC_VER"/*.tar.* "$SRC/prereq/" 2>/dev/null || true

	mkdir -p "$WORK/build-gcc"
	cd "$WORK/build-gcc"
	"$WORK/gcc-$GCC_VER/configure" "${GCC_CONFIGURE[@]}"
	make -j"$JOBS" MAKEINFO=true all-gcc
	make install-gcc MAKEINFO=true

	# The compile-side specs, before libgcc, so libgcc is built with the same
	# struct layout as everything else (see toolchain/install-specs.sh, whose
	# cpp, cc1 and self_spec entries these are, with the reasons).  The link
	# entries there are for AMIX programs and are not used: a kernel links
	# with -nostdlib and its own script.  The build tree's xgcc reads its own
	# copy, hence the second file.
	SPECS="$PREFIX/lib/gcc/$TARGET/$GCC_VER/specs"
	"$PREFIX/bin/$TARGET-gcc" -dumpspecs > "$SPECS.new"
	python3 - "$SPECS.new" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
def replace(section, value):
    global text
    key = "*%s:\n" % section
    i = text.index(key) + len(key)
    j = text.index("\n\n", i)
    text = text[:i] + value + text[j:]
replace("cpp", "-D__amix__ -D__amix -Damix")
replace("cc1", "%(cc1_cpu) -Wno-error=implicit-int")
replace("self_spec", "%{!mno-align-int:-malign-int} %{!mstrict-align:-mno-strict-align}")
open(path, "w").write(text)
PY
	mv "$SPECS.new" "$SPECS"
	cp "$SPECS" "$WORK/build-gcc/gcc/specs"
	# The specs copy makes the gcc directory out of date (specs is in
	# GCC_PASSES); bring it up to date now rather than inside libgcc's build.
	make -j"$JOBS" MAKEINFO=true all-gcc

	make -j"$JOBS" MAKEINFO=true all-target-libgcc
	make install-target-libgcc MAKEINFO=true
	cd "$REPO"
	echo "$GCC_SUM" > "$GCC_STAMP"
	echo "== gcc $GCC_VER installed"
fi

# libgcc is what a kernel links for 64-bit arithmetic, so it has to run on
# every CPU in the range: no 64-bit MULU.L/DIVU.L for the 68060, no
# 68040/060-only FPU forms for the 68881/68882.  Checked on every run, from a
# listing made by this toolchain's own objdump.
GCCLIB="$PREFIX/lib/gcc/$TARGET/$GCC_VER"
LISTING="$WORK/libgcc.dis"
"$PREFIX/bin/$TARGET-objdump" -d "$GCCLIB/libgcc.a" "$GCCLIB"/crt*.o > "$LISTING"
python3 "$REPO/tools/check-68060-safe.py" --68881 "$LISTING" || {
	echo "!! libgcc is not safe on every 68020..68060 + 68881 machine; see above." >&2
	exit 1
}

echo
echo "toolchain: $PREFIX/bin/$TARGET-gcc"
"$PREFIX/bin/$TARGET-gcc" --version | head -1
"$PREFIX/bin/$TARGET-as" --version | head -1
