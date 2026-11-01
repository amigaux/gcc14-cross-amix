/* solib-main-rt.c -- check libamixrt's functions on the machine.
 *
 * Linked by tools/test-amixrt.sh against libsolibrtmid.so.1 only.  The
 * functions under test are called from libsolibrt, which the program reaches
 * through libsolibrtmid's NEEDED, so they are libamixrt.so.1's own and not
 * copies the program linked.  The program calls none of them itself, so it
 * has no copy for the runtime linker to prefer.  One line per check, then
 * PASS or FAIL; exit status 0 when all pass.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

long mid_now(void);
long mid_select_sleep_us(long);
long mid_usleep_us(long);
int mid_select_pipe(void);
long mid_truncate(const char *);
char *mid_realpath(const char *, char *);
int mid_setitimer(long, long);
long mid_getitimer(void);
int mid_tables(void);
int mid_snprintf(char *, int);

static int failures;
static volatile int alarms;

static void report(const char *what, int ok, const char *detail)
{
	printf("%-48s %s%s%s\n", what, ok ? "ok" : "FAIL", detail ? "  " : "",
	       detail ? detail : "");
	if (!ok) failures++;
}

static void on_alarm(int sig)
{
	alarms++;
	signal(sig, on_alarm);
}

/* Wait until alarms reaches n or secs seconds pass. */
static void wait_alarms(int n, long secs)
{
	long end = mid_now() + secs;

	while (alarms < n && mid_now() <= end) pause();
}

int main(void)
{
	char buf[1024], base[1024], want[1100], tmp[64], d[96];
	long t, us;
	char *m;

	t = mid_now();
	{
		time_t s = (time_t)t;

		printf("the machine's clock reads %s", ctime(&s));
	}
	sprintf(d, "%ld (needs the machine's date set after 2020-01-01)", t);
	report("gettimeofday: a date after 2020", t > 1577836800L, d);

	us = mid_select_sleep_us(200);
	sprintf(d, "%ld us", us);
	report("select: no descriptors sleeps 200 ms", us >= 190000 && us < 2000000, d);
	report("select: a pipe is readable once it has data", mid_select_pipe(), 0);

	us = mid_usleep_us(100000);
	sprintf(d, "%ld us", us);
	report("usleep (libamixc99, over select): 100 ms", us >= 90000 && us < 2000000, d);

	sprintf(tmp, "/tmp/amixrt-trunc.%ld", (long)getpid());
	t = mid_truncate(tmp);
	sprintf(d, "%ld (1003)", t);
	report("truncate to 10, then ftruncate to 3", t == 1003, d);

	sprintf(tmp, "/tmp/amixrt-path.%ld", (long)getpid());
	if (mkdir(tmp, 0755) == 0 && chdir(tmp) == 0 && mkdir("d", 0755) == 0
	    && symlink("d", "l") == 0 && getcwd(base, sizeof base) != 0) {
		sprintf(want, "%s/d", base);
		m = mid_realpath("l/./../l", buf);
		report("realpath follows a link and drops . and ..",
		       m != 0 && strcmp(buf, want) == 0, m ? buf : "NULL");
		m = mid_realpath("l", 0);
		report("realpath(path, NULL) allocates", m != 0 && strcmp(m, want) == 0, m);
		free(m);
		m = mid_realpath("nothere/x", buf);
		report("realpath of a missing name fails with ENOENT",
		       m == 0 && errno == ENOENT, 0);
		unlink("l");
		rmdir("d");
		chdir("/");
		rmdir(tmp);
	} else {
		report("realpath: setting up the directory", 0, tmp);
	}

	signal(SIGALRM, on_alarm);
	report("setitimer: repeating, 1 s", mid_setitimer(1, 1) == 0, 0);
	t = mid_getitimer();
	sprintf(d, "%ld (1000000 + 1..1000)", t);
	report("getitimer: the period kept, time left counted",
	       t > 1000000 && t <= 1001000, d);
	wait_alarms(2, 4);
	sprintf(d, "%d alarms", alarms);
	report("the timer fires repeatedly", alarms >= 2, d);
	report("setitimer: disarm", mid_setitimer(0, 0) == 0, 0);
	t = mid_getitimer();
	sprintf(d, "%ld", t);
	report("getitimer: nothing left after disarming", t == 0, d);
	alarms = 0;
	mid_setitimer(0, 1);
	wait_alarms(1, 3);
	sleep(2);
	sprintf(d, "%d alarm(s)", alarms);
	report("setitimer: a one-shot fires once", alarms == 1, d);

	report("sys_errlist and _sys_siglist read as libc's", mid_tables(), 0);
	t = mid_snprintf(buf, 8);
	sprintf(d, "%ld, \"%s\"", t, buf);
	report("snprintf: %lld truncated to 7 characters",
	       t == 13 && strcmp(buf, "1234567") == 0, d);

	printf("solib-main-rt: %s\n", failures ? "FAIL" : "PASS");
	fflush(stdout);
	return failures != 0;
}
