# host-shim

Headers for compiling libamixrt's portable members on the build host
(`tools/test-compat-host.sh`, `probe/amixrt-libc-host.c`). They declare only
what those members use, with AMIX's record layouts written as fixed-size
fields, so that the files the host test writes have AMIX's record sizes. They
are written from the documented interfaces (utmpx(4), mnttab(4), log(7),
putmsg(2)), not copied from AMIX's headers, and are never used for the target.
