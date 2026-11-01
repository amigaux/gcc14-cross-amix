#!/bin/bash
# build-compat.sh -- build libamixc99.a, the parts of a C library that AMIX
# SVR4.0 does not have, and install it into the toolchain.
#
# It is installed rather than left in a build directory, and named in the
# compiler's own lib spec, because these are libc functions: code that calls
# snprintf should link, without every project having to know that this target
# needs an extra library.  That is also why the name says what it is -- the C99
# and POSIX pieces AMIX predates -- rather than naming a project.
#
# Not a Makefile: config.sh is shell, and a Makefile that `include`s it gets the
# quotes as part of the value.
#
# The name is deliberately not libamixcompat.a: another AMIX cross toolchain
# installs one of those into the shared sysroot, holding syscall wrappers and
# floating-point control, and overwriting it would break every other project
# using that sysroot.
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "build-compat.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"

if [ -x "$TC_PREFIX/bin/$TC_TARGET-gcc" ]; then
	CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
	AR="$TC_PREFIX/bin/$TC_TARGET-ar"
	INC=""
else
	echo "build-compat.sh: using the fallback compiler ($MODERN_GCC)"
	CC="$MODERN_GCC"
	AR="${MODERN_PREFIX}ar"
	INC="-nostdinc -isystem $AMIX_SYSROOT/usr/include"
fi

OUT=build/compat
mkdir -p "$OUT"
# No $AMIX_CPU here: that is the CPU the probes are built for, and this is
# runtime, linked into every program whatever it was built for.  It follows
# the compiler's default CPU, as libgcc does.  Built with -mcpu=68060, one
# double in amix_vsnprintf became an fdmove, which a 68881/68882 does not have
# (it now moves that double as two words and names no FP register at all).
# amix_init.c is a STARTFILE, not a library member: it is named in the specs
# and linked into every program, because nothing would ever reference it.
$CC -O2 $INC -U__linux__ -U__gnu_linux__ -Ulinux -D__amix__ -D__svr4__ \
	-c -o "$OUT/amix_init.o" compat/amix_init.c
# mkdir rather than "install only if the directory happens to exist": skipping
# the install in silence leaves a toolchain whose specs name a startfile that
# is not there, and the failure surfaces much later as a link error in an
# unrelated project.
mkdir -p "$TC_PREFIX/$TC_TARGET/lib"
cp "$OUT/amix_init.o" "$TC_PREFIX/$TC_TARGET/lib/amix_init.o"
echo "build-compat.sh: installed $TC_PREFIX/$TC_TARGET/lib/amix_init.o"
# amix_initS.o is the same file built for shared objects, which gcc -shared
# links instead.  Built in its own directory, so that the archive below, which
# takes every object in $OUT, does not pick it up.
mkdir -p "$OUT/pic"
$CC -O2 -fPIC $INC -U__linux__ -U__gnu_linux__ -Ulinux -D__amix__ -D__svr4__ \
	-c -o "$OUT/pic/amix_initS.o" compat/amix_init.c
cp "$OUT/pic/amix_initS.o" "$TC_PREFIX/$TC_TARGET/lib/amix_initS.o"
echo "build-compat.sh: installed $TC_PREFIX/$TC_TARGET/lib/amix_initS.o"

# libamixc99.a: what the program links statically.  Named member by member,
# so an object left in $OUT by an older build cannot slip into the archive.
# amix_dso.o, the deferral runner, is no longer a member: it moved into
# amix_init.o above.
C99_MEMBERS="amix_compat amix_cxa amix_str amix_dlopen"
for m in $C99_MEMBERS; do
	$CC -O2 -fno-strict-aliasing $INC \
		-U__linux__ -U__gnu_linux__ -Ulinux -D__amix__ -D__svr4__ \
		-c -o "$OUT/$m.o" "compat/$m.c"
done
rm -f "${OUT:?}/libamixc99.a"
$AR rcs "$OUT/libamixc99.a" $(for m in $C99_MEMBERS; do echo "$OUT/$m.o"; done)

if [ -d "$TC_PREFIX/$TC_TARGET/lib" ]; then
	cp "$OUT/libamixc99.a" "$TC_PREFIX/$TC_TARGET/lib/libamixc99.a"
	echo "build-compat.sh: installed $TC_PREFIX/$TC_TARGET/lib/libamixc99.a"
else
	echo "build-compat.sh: $OUT/libamixc99.a (toolchain not built yet, not installed)"
fi

# libamixrt.so.1: the same C99 pieces, built position-independent, plus our
# own implementations of the libc functions AMIX has only as static,
# non-PIC members of usr/ccs/lib/libc.so.  Every shared library built with
# `gcc -shared` NEEDs it when it uses one of them (the specs link it
# --as-needed), so a library reached only through another library's NEEDED,
# or loaded by dlopen, still finds them.  docs/TOOLCHAIN.md, "libamixrt.so:
# the runtime a shared library needs", has the rules; in short it defines
# nothing libc.so.1 exports, has no constructors and no DT_INIT, NEEDs
# nothing, and must have no text relocations.
#
# It needs libgcc.a (the 64-bit divide in the formatter), which a fresh
# toolchain build has not made yet when it first runs this script;
# build-cross.sh runs it again once libgcc is installed.
RT_MEMBERS="amix_compat amix_cxa amix_str amix_dlopen amixrt_time amixrt_select \
	amixrt_truncate amixrt_realpath amixrt_itimer"
LIBGCC=$($CC -print-libgcc-file-name 2>/dev/null || true)
if [ -z "$INC" ] && [ -f "$LIBGCC" ]; then
	RT="$OUT/pic/rt"
	mkdir -p "$RT"
	python3 tools/gen-amixrt-tables.py "$AMIX_SYSROOT/usr/ccs/lib/libc.so" > "$RT/amixrt_tables.c"
	RT_OBJS=""
	for m in $RT_MEMBERS; do
		$CC -O2 -fPIC -fno-strict-aliasing \
			-U__linux__ -U__gnu_linux__ -Ulinux -D__amix__ -D__svr4__ \
			-c -o "$RT/$m.o" "compat/$m.c"
		RT_OBJS="$RT_OBJS $RT/$m.o"
	done
	$CC -O2 -fPIC -c -o "$RT/amixrt_tables.o" "$RT/amixrt_tables.c"
	$CC -c -o "$RT/amixrt_syscall.o" compat/amixrt_syscall.S
	# -nostartfiles: no crti/crtn, so no DT_INIT or DT_FINI, and nothing of
	# crtbeginS.o; the library initialises nothing.  -nodefaultlibs: not
	# linked against itself, and not against libc, which it must not NEED
	# (the runtime linker knows libc.so.1 only as /usr/lib/libc.so.1).
	# -z text turns a text relocation into an error.
	rm -f "${RT:?}/libamixrt.so.1"
	$CC -shared -nostartfiles -nodefaultlibs -Wl,-soname,libamixrt.so.1 \
		-Wl,-z,text -o "$RT/libamixrt.so.1" \
		$RT_OBJS "$RT/amixrt_tables.o" "$RT/amixrt_syscall.o" -lgcc
	# Installed in a directory of its own, which the specs find with
	# %:find-file(amixrt): -L for `gcc -shared`, -rpath-link for every link.
	RTDIR="$TC_PREFIX/$TC_TARGET/lib/amixrt"
	mkdir -p "$RTDIR"
	cp "$RT/libamixrt.so.1" "$RTDIR/libamixrt.so.1"
	ln -sf libamixrt.so.1 "$RTDIR/libamixrt.so"
	echo "build-compat.sh: installed $RTDIR/libamixrt.so.1"
else
	echo "build-compat.sh: libamixrt.so.1 not built (no libgcc.a yet, or not this toolchain's compiler)"
fi
