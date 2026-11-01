/* solib-rt.c -- a shared library that uses every libamixrt function.
 *
 * Built by tools/test-amixrt.sh as libsolibrt.so.1.  It is reached only
 * through libsolibrtmid (probe/solib-rtmid.c), so the program's link cannot
 * supply these functions and they come from libamixrt.so.1 -- which is the
 * point: on AMIX this exercises our own gettimeofday, select, truncate,
 * realpath, the interval timers, the message tables and the C99 pieces,
 * rather than libc's.  Each function returns what it saw; the program
 * (probe/solib-main-rt.c) judges.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* AMIX's headers declare none of these. */
extern int gettimeofday(struct timeval *, void *);
extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
extern char *sys_errlist[];
extern int sys_nerr;

long rt_now(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, 0) != 0) return -1;
	return tv.tv_sec;
}

/* Microseconds between two gettimeofday calls around f(arg). */
static long rt_elapsed(void (*f)(long), long arg)
{
	struct timeval a, b;

	gettimeofday(&a, 0);
	f(arg);
	gettimeofday(&b, 0);
	return (b.tv_sec - a.tv_sec) * 1000000L + (b.tv_usec - a.tv_usec);
}

static void rt_select_sleep(long ms)
{
	struct timeval tv;

	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	select(0, 0, 0, 0, &tv);
}

static void rt_usleep(long us)
{
	usleep((unsigned long)us);
}

long rt_select_sleep_us(long ms)
{
	return rt_elapsed(rt_select_sleep, ms);
}

long rt_usleep_us(long us)
{
	return rt_elapsed(rt_usleep, us);
}

/* 1 if select reports a pipe readable once, and only once, it has data. */
int rt_select_pipe(void)
{
	int p[2], before, after;
	fd_set rd;
	struct timeval tv;

	if (pipe(p) != 0) return 0;
	FD_ZERO(&rd);
	FD_SET(p[0], &rd);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	before = select(p[0] + 1, &rd, 0, 0, &tv);
	write(p[1], "x", 1);
	FD_ZERO(&rd);
	FD_SET(p[0], &rd);
	after = select(p[0] + 1, &rd, 0, 0, &tv);
	close(p[0]);
	close(p[1]);
	return before == 0 && after == 1 && FD_ISSET(p[0], &rd);
}

/* The size after truncate to 10 and ftruncate to 3, as 10 * 100 + 3. */
long rt_truncate(const char *path)
{
	struct stat st;
	long a, b;
	int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);

	if (fd < 0) return -1;
	write(fd, "0123456789012345678901234567890123456789", 40);
	close(fd);
	if (truncate(path, 10) != 0 || stat(path, &st) != 0) return -2;
	a = st.st_size;
	fd = open(path, O_WRONLY);
	if (fd < 0 || ftruncate(fd, 3) != 0) return -3;
	close(fd);
	if (stat(path, &st) != 0) return -4;
	b = st.st_size;
	unlink(path);
	return a * 100 + b;
}

char *rt_realpath(const char *path, char *buf)
{
	return realpath((char *)path, buf);
}

int rt_setitimer(long interval, long value)
{
	struct itimerval v;

	v.it_interval.tv_sec = interval;
	v.it_interval.tv_usec = 0;
	v.it_value.tv_sec = value;
	v.it_value.tv_usec = 0;
	return setitimer(ITIMER_REAL, &v, 0);
}

/* it_interval.tv_sec * 1000 + it_value in ms, or -1. */
long rt_getitimer(void)
{
	struct itimerval v;

	if (getitimer(ITIMER_REAL, &v) != 0) return -1;
	return v.it_interval.tv_sec * 1000000L + v.it_value.tv_sec * 1000L
	       + v.it_value.tv_usec / 1000;
}

/* 1 if libamixrt's tables read as libc.so.1's strerror does. */
int rt_tables(void)
{
	return sys_nerr > ENOENT && strcmp(sys_errlist[ENOENT], strerror(ENOENT)) == 0
	       && _sys_nsig > SIGINT && strcmp(strsignal(SIGINT), _sys_siglist[SIGINT]) == 0;
}

/* snprintf's return for a truncated 64-bit conversion, and what it kept. */
int rt_snprintf(char *buf, int len)
{
	return snprintf(buf, len, "%lld", 1234567890123LL);
}
