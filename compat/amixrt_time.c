/* amixrt_time.c -- gettimeofday and settimeofday, for libamixrt.so.
 *
 * AMIX has both only as a static member of usr/ccs/lib/libc.so, which cannot
 * go into a shared library: it is not position-independent.  These are
 * written from the system-call interface in <sys/hrtcntl.h> and
 * <sys/syscall.h> and behave as libc's do: the time comes from
 * hrtcntl(HRT_TOFD) on the standard clock in microseconds, the time zone
 * argument is ignored, and settimeofday sets whole seconds with stime(2),
 * rounding the microseconds.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <errno.h>
#include <unistd.h>

extern int __amixrt_syscall(int, ...);
extern int stime(const time_t *);

/* <sys/hrtcntl.h> cannot be included here: it declares a structure with an
 * ecb_t, which no user header defines.  These are its values. */
struct amixrt_hrtime {
	unsigned long secs;
	long rem;
	unsigned long res;
};
#define AMIXRT_HRTCNTL	0	/* hrtsys subcode */
#define AMIXRT_HRT_TOFD	1	/* hrtcntl command: time of day */
#define AMIXRT_CLK_STD	1	/* the real-time clock */

int gettimeofday(struct timeval *tv, void *tz)
{
	struct amixrt_hrtime t;

	(void)tz;
	if (tv == 0) return 0;
	t.secs = 0;
	t.rem = 0;
	t.res = 1000000;
	if (__amixrt_syscall(SYS_hrtsys, AMIXRT_HRTCNTL, AMIXRT_HRT_TOFD,
			     AMIXRT_CLK_STD, (void *)0, &t) != 0)
		return -1;
	tv->tv_sec = (long)t.secs;
	tv->tv_usec = t.rem;
	return 0;
}

int settimeofday(struct timeval *tv, void *tz)
{
	time_t t;

	(void)tz;
	if (tv == 0) return 0;
	if (tv->tv_sec < 0 || tv->tv_usec < 0 || tv->tv_usec > 999999) {
		errno = EINVAL;
		return -1;
	}
	t = tv->tv_sec;
	if (tv->tv_usec >= 500000) t++;
	return stime(&t);
}
