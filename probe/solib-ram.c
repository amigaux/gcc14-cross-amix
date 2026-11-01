/* solib-ram.c -- the smallest library, with and without libamixrt.
 *
 * Built twice by tools/test-amixrt.sh: as libsolibram.so.1, which calls only
 * what libc.so.1 exports, and with -DUSE_AMIXRT as libsolibramrt.so.1, which
 * calls snprintf and so NEEDs libamixrt.so.1.  probe/solib-main-ram.c is
 * linked against each and sleeps, so the memory one process costs can be
 * compared on the machine with and without libamixrt mapped.
 */
#include <stdio.h>

int ram_fmt(char *buf, int len, int v)
{
#ifdef USE_AMIXRT
	return snprintf(buf, len, "%d", v);
#else
	(void)len;
	return sprintf(buf, "%d", v);
#endif
}
