/* solib-state.c -- a shared library that uses libc functions with state.
 *
 * Built by tools/test-amixrt.sh as libsolibstate.so.1 and reached only
 * through libsolibstatemid (probe/solib-statemid.c).  drand48's seed,
 * syslog's mask and the interval timers' kept period are per-process state:
 * a library and the program that seeds, masks or arms them must work on the
 * same copy.  probe/solib-main-interpose.c sets each in the program and reads
 * it back here, and the other way round.
 *
 * drand48 and syslog are not in libamixrt: the program supplies them, as it
 * does whenever it calls them itself.  setitimer and getitimer are in
 * libamixrt too, so this is also the case where two copies exist in the
 * process and the program's must win.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>

extern int setlogmask(int);	/* not declared by AMIX's <syslog.h> */

/* The syslog functions need openlog first (the program calls it): libc's
 * setlogmask does not check for the state openlog allocates. */

long st_lrand48(void)
{
	return lrand48();
}

void st_srand48(long seed)
{
	srand48(seed);
}

/* The current mask: setlogmask(0) reports it without changing it. */
int st_logmask(void)
{
	return setlogmask(0);
}

int st_setlogmask(int mask)
{
	return setlogmask(mask);
}

/* The kept period, in seconds, or -1. */
long st_itimer_interval(void)
{
	struct itimerval v;

	if (getitimer(ITIMER_REAL, &v) != 0) return -1;
	return v.it_interval.tv_sec;
}

int st_setitimer(long interval, long value)
{
	struct itimerval v;

	v.it_interval.tv_sec = interval;
	v.it_interval.tv_usec = 0;
	v.it_value.tv_sec = value;
	v.it_value.tv_usec = 0;
	return setitimer(ITIMER_REAL, &v, 0);
}
