/* solib-dep.c -- a C shared library that is reached only through another one.
 *
 * Built by tools/test-shared-indirect.sh as libsolibdep.so.1.  libsolibmid
 * (probe/solib-mid.c) is linked against it and so NEEDs it; the program links
 * only -lsolibmid.  That is the X11 shape: a client names -lXmu, and libXmu
 * NEEDs libXt.
 *
 * It calls two things the runtime does not export from libc.so.1:
 * gettimeofday, which AMIX has only as a static member of usr/ccs/lib/libc.so,
 * and snprintf, which is in libamixc99.a.  Like every library built with
 * `gcc -shared` it leaves both undefined for the program to supply.
 */
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

static int ready;

__attribute__((constructor)) static void solib_dep_init(void)
{
	static const char msg[] = "solib-dep: constructor ran (write)\n";

	write(1, msg, sizeof msg - 1);
	ready = 1;
	printf("solib-dep: constructor ran (stdio)\n");
	fflush(stdout);
}

int solib_dep_ready(void)
{
	return ready;
}

/* Seconds since 1970, formatted: 1 if the clock looks like a date after
 * 2020-01-01, so a wrong system call shows as 0. */
int solib_dep_clock(char *buf, int len)
{
	struct timeval tv;

	if (gettimeofday(&tv, 0) != 0) return 0;
	snprintf(buf, len, "%ld.%06ld", (long)tv.tv_sec, (long)tv.tv_usec);
	return tv.tv_sec > 1577836800L && tv.tv_usec >= 0 && tv.tv_usec < 1000000;
}
