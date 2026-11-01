/* solib-main-interpose.c -- a program and a library share libc's state.
 *
 * Linked by tools/test-amixrt.sh against libsolibstatemid.so.1 only; the
 * library using the state, libsolibstate, is reached through its NEEDED.  The
 * program calls srand48, setlogmask and setitimer itself, so its link copies
 * libc's static members in, and ld exports them because libsolibstate refers
 * to them.  The runtime linker looks in the program first, so the library has
 * to land on the program's copies -- and not on libamixrt's setitimer, which
 * is loaded too.  Each check sets the state on one side and reads it on the
 * other.  One line per check, then PASS or FAIL; exit status 0 when all pass.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

/* AMIX's <syslog.h> declares neither. */
extern int setlogmask(int);
extern void openlog(const char *, int, int);

long mid_lrand48(void);
void mid_srand48(long);
int mid_logmask(void);
int mid_setlogmask(int);
long mid_itimer_interval(void);
int mid_setitimer(long, long);

static int failures;

static void report(const char *what, int ok, long got, long want)
{
	printf("%-52s %s  got %ld, want %ld\n", what, ok ? "ok" : "FAIL", got, want);
	if (!ok) failures++;
}

static int arm(long interval, long value)
{
	struct itimerval v;

	v.it_interval.tv_sec = interval;
	v.it_interval.tv_usec = 0;
	v.it_value.tv_sec = value;
	v.it_value.tv_usec = 0;
	return setitimer(ITIMER_REAL, &v, 0);
}

static long interval(void)
{
	struct itimerval v;

	return getitimer(ITIMER_REAL, &v) == 0 ? v.it_interval.tv_sec : -1;
}

int main(void)
{
	long v;
	int m;

	/* 483889296 and 1973930609 are the first two lrand48 values after
	 * srand48(12345), by the algorithm's definition. */
	srand48(12345);
	v = mid_lrand48();
	report("drand48: the program seeds, the library draws", v == 483889296L, v, 483889296L);
	v = lrand48();
	report("drand48: the program draws the next one", v == 1973930609L, v, 1973930609L);
	mid_srand48(12345);
	v = lrand48();
	report("drand48: the library seeds, the program draws", v == 483889296L, v, 483889296L);

	/* openlog first: libc's setlogmask uses the state openlog allocates and
	 * does not check for it, so before openlog it follows a null pointer --
	 * a SIGSEGV on a kernel that does not map page zero. */
	openlog("solib-main-interpose", 0, LOG_USER);
	setlogmask(0x3f);
	m = mid_logmask();
	report("syslog: the program sets the mask, the library sees it", m == 0x3f, m, 0x3f);
	mid_setlogmask(0x0f);
	m = setlogmask(0);
	report("syslog: the library sets the mask, the program sees it", m == 0x0f, m, 0x0f);

	arm(7, 100);
	v = mid_itimer_interval();
	report("itimer: the program arms, the library sees the period", v == 7, v, 7);
	mid_setitimer(5, 100);
	v = interval();
	report("itimer: the library arms, the program sees the period", v == 5, v, 5);
	arm(0, 0);

	printf("solib-main-interpose: %s\n", failures ? "FAIL" : "PASS");
	fflush(stdout);
	return failures != 0;
}
