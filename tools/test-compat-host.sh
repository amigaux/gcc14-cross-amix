#!/bin/bash
# test-compat-host.sh -- run the compat layer's batteries on THIS machine:
# the formatter, then the C++ destructor list.
#
# The formatter is ordinary portable C, so its logic can be tested natively in
# a second rather than over a TFTP round trip to the Amiga. Only usleep is
# target-specific, and it is excluded here.
#
# This is the same argument as writing the X11 video driver on Linux first: the
# Amiga is for questions only the Amiga can answer.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/hosttest
cc -O1 -g -Wall -Wextra -Wno-unused-parameter -DAMIX_COMPAT_HOST_TEST \
   -o build/hosttest/compat probe/compat.c compat/amix_compat.c
./build/hosttest/compat
# The C++ destructor list.  Its symbols are renamed, so the host C library's
# own __cxa_atexit stays in charge of this test program.
cc -O1 -g -Wall -Wextra -Wno-prio-ctor-dtor \
   -D__cxa_atexit=amix_test_cxa_atexit -D__cxa_finalize=amix_test_cxa_finalize \
   -D__dso_handle=amix_test_dso_handle \
   -o build/hosttest/cxa probe/cxa.c compat/amix_cxa.c
exec ./build/hosttest/cxa
