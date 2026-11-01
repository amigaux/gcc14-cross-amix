#!/bin/bash
# test-shared-indirect.sh -- link a program against a shared library that
# needs another shared library, naming only the first.
#
# The shape every library stack has: a client links -lXmu, and libXmu NEEDs
# libXt.  probe/solib-dep.c becomes libsolibdep.so.1; probe/solib-mid.c
# becomes libsolibmid.so.1, linked against it, so it records
# DT_NEEDED libsolibdep.so.1; probe/solib-main-indirect.c links -lsolibmid
# alone.
#
# libsolibdep calls gettimeofday, which AMIX has only as a static member of
# usr/ccs/lib/libc.so, and snprintf, from libamixc99.a.  GNU ld fills a
# library's undefined references from archives only for libraries named on the
# command line: a library it loads because another one NEEDs it is read after
# the archives have been searched.  So the libraries take both from
# libamixrt.so.1 instead, which `gcc -shared` records as NEEDED when a library
# uses it, and which the program's link finds with -rpath-link.  Checked: the
# program links, everything the closure leaves undefined is defined at run
# time, and the program itself does not NEED libamixrt.so.1.  Naming
# -lsolibdep as well is checked too, as the control.
#
# Nothing runs on the host.  The result is staged in build/test-shared-indirect/
# with libamixrt.so.1 and run-shared-indirect.sh, for a run on AMIX:
#
#   copy build/test-shared-indirect/ to the machine, then there:
#   sh run-shared-indirect.sh
#
# Expected there: solib-dep's "(write)" and "(stdio)" lines, then solib-mid's
# with "solib-dep ready=1" -- the library a library NEEDs is initialised
# first -- then "constructor order: solib-dep before solib-mid", the clock
# line ending in PASS, and "exit status: 0".
set -e
cd "$(dirname "$0")/.."
# sort and comm have to agree on the order.
export LC_ALL=C
[ -f config.sh ] || { echo "test-shared-indirect.sh: no config.sh" >&2; exit 1; }
. ./config.sh
TC_TARGET="${TC_TARGET:-m68k-elf}"
CC="$TC_PREFIX/bin/$TC_TARGET-gcc"
READELF="$TC_PREFIX/bin/$TC_TARGET-readelf"
NM="$TC_PREFIX/bin/$TC_TARGET-nm"
OUT=build/test-shared-indirect
rm -rf "${OUT:?}"
mkdir -p "$OUT"
RTDIR=$("$CC" -print-file-name=amixrt)
[ -f "$RTDIR/libamixrt.so.1" ] || { echo "test-shared-indirect.sh: no libamixrt.so.1 in $RTDIR -- run tools/build-compat.sh" >&2; exit 1; }
cp "$RTDIR/libamixrt.so.1" "$OUT/"

FAILED=0
pass() { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; FAILED=1; }

echo "== building the libraries"
"$CC" -O2 -fPIC -shared -Wl,-soname,libsolibdep.so.1 -o "$OUT/libsolibdep.so.1" probe/solib-dep.c
"$CC" -O2 -fPIC -shared -Wl,-soname,libsolibmid.so.1 -o "$OUT/libsolibmid.so.1" probe/solib-mid.c \
	"$OUT/libsolibdep.so.1"
if "$READELF" -d "$OUT/libsolibmid.so.1" | grep -qE '\(NEEDED\) +Shared library: \[libsolibdep\.so\.1\]'; then
	pass "libsolibmid NEEDs libsolibdep.so.1"
else
	fail "libsolibmid NEEDs libsolibdep.so.1"
fi

echo "== control: the program names both libraries"
if "$CC" -O2 -o "$OUT/control" probe/solib-main-indirect.c \
	"$OUT/libsolibmid.so.1" "$OUT/libsolibdep.so.1" 2> "$OUT/control.log"; then
	pass "links"
else
	fail "links"
	sed 's/^/        /' "$OUT/control.log"
fi
rm -f "${OUT:?}/control" "${OUT:?}/control.log"

echo "== the program names only libsolibmid"
PROG="$OUT/solib-main-indirect"
if "$CC" -O2 -o "$PROG" probe/solib-main-indirect.c "$OUT/libsolibmid.so.1" \
	-Wl,-rpath-link,"$OUT" 2> "$OUT/link.log"; then
	pass "links"
	LINKED=1
	if "$READELF" -d "$PROG" | grep -q 'libamixrt\.so\.1'; then
		fail "the program does not NEED libamixrt.so.1"
	else
		pass "the program does not NEED libamixrt.so.1"
	fi
	if "$READELF" -d "$OUT/libsolibdep.so.1" | grep -qE '\(NEEDED\) +Shared library: \[libamixrt\.so\.1\]'; then
		pass "libsolibdep NEEDs libamixrt.so.1"
	else
		fail "libsolibdep NEEDs libamixrt.so.1"
	fi
else
	fail "links"
	sed -n 's/.*: \(undefined reference to .*\)/        \1/p' "$OUT/link.log"
	LINKED=0
fi
rm -f "${OUT:?}/link.log"

if [ "$LINKED" = 1 ]; then
	# Everything the two libraries leave undefined has to be found at run
	# time: in the program's exports, in a library the runtime linker loads
	# (the program's NEEDED entries and theirs, followed transitively), or in
	# libc.so.1, whose symbol table is its dynamic one, so plain nm reads it.
	CLOSURE=""
	TODO=$("$READELF" -d "$PROG" | sed -nE 's/.*\(NEEDED\) +Shared library: \[([^]/]+)\]/\1/p')
	while [ -n "$TODO" ]; do
		NEXT=""
		for l in $TODO; do
			case " $CLOSURE " in *" $l "*) continue ;; esac
			[ -f "$OUT/$l" ] || continue
			CLOSURE="$CLOSURE $l"
			NEXT="$NEXT $("$READELF" -d "$OUT/$l" | sed -nE 's/.*\(NEEDED\) +Shared library: \[([^]/]+)\]/\1/p')"
		done
		TODO="$NEXT"
	done
	DEFS="$OUT/defs"
	{ "$NM" -D --defined-only "$PROG"
	  for l in $CLOSURE; do "$NM" -D --defined-only "$OUT/$l"; done
	  "$NM" --defined-only "$AMIX_SYSROOT/usr/lib/libc.so.1"
	} | awk '$2 ~ /^[A-Z]$/ {print $3}' | sort -u > "$DEFS"
	MISSING=$(for l in $CLOSURE; do "$NM" -D --undefined-only "$OUT/$l"; done \
		| awk '$1 == "U" {print $2}' | sort -u | comm -23 - "$DEFS")
	rm -f "${DEFS:?}"
	if [ -z "$MISSING" ]; then
		pass "every undefined symbol of$CLOSURE is defined at run time"
	else
		fail "left undefined at run time: $(echo $MISSING)"
	fi
	if "$READELF" -d "$PROG" | grep -q TEXTREL || \
	   for l in $CLOSURE; do "$READELF" -d "$OUT/$l"; done | grep -q TEXTREL; then
		fail "no text relocations in the program or its libraries"
	else
		pass "no text relocations in the program or its libraries"
	fi
fi

cat > "$OUT/run-shared-indirect.sh" <<'EOF'
# run-shared-indirect.sh -- run on AMIX, in the directory
# test-shared-indirect.sh staged.
LD_LIBRARY_PATH=`pwd`
export LD_LIBRARY_PATH
./solib-main-indirect
echo "exit status: $?"
EOF

echo
if [ "$FAILED" = 0 ]; then
	echo "test-shared-indirect.sh: all checks passed; staged in $OUT for a run on AMIX"
else
	echo "test-shared-indirect.sh: FAILED"
	exit 1
fi
