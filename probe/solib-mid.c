/* solib-mid.c -- a C shared library that NEEDs libsolibdep.
 *
 * Built by tools/test-shared-indirect.sh as libsolibmid.so.1, linked with
 * -lsolibdep so that it records DT_NEEDED libsolibdep.so.1.  The program links
 * only this library.
 *
 * Its constructor reads libsolibdep's state.  The runtime linker initialises
 * objects in load order, which puts this library ahead of the one it depends
 * on, so the value it saw tells which order the constructors really ran in.
 */
#include <stdio.h>
#include <unistd.h>

int solib_dep_ready(void);
int solib_dep_clock(char *, int);

static int dep_ready_at_init = -1;

__attribute__((constructor)) static void solib_mid_init(void)
{
	static const char msg[] = "solib-mid: constructor ran (write)\n";

	write(1, msg, sizeof msg - 1);
	dep_ready_at_init = solib_dep_ready();
	printf("solib-mid: constructor ran (stdio), solib-dep ready=%d\n",
	       dep_ready_at_init);
	fflush(stdout);
}

/* 1 if libsolibdep's constructor had run before this library's. */
int solib_mid_dep_first(void)
{
	return dep_ready_at_init;
}

int solib_mid_clock(char *buf, int len)
{
	return solib_dep_clock(buf, len);
}
