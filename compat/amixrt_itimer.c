/* amixrt_itimer.c -- getitimer and setitimer, for libamixrt.so.
 *
 * SVR4.0 implements the BSD interval timers in the library, over the
 * hrtalarm system call's BSD commands, and AMIX has that code only as a
 * static member of usr/ccs/lib/libc.so.  This follows the same protocol,
 * written from <sys/hrtcntl.h>:
 *
 *   - ITIMER_REAL, ITIMER_VIRTUAL and ITIMER_PROF are the clocks CLK_STD,
 *     CLK_USERVIRT and CLK_PROCVIRT;
 *   - HRT_BSD arms a one-shot timer, HRT_BSD_REP a repeating one (first
 *     expiry in hrtc_tod, period in hrtc_int), HRT_BSD_CANCEL disarms, and
 *     HRT_BSD_PEND reads the time left, failing with EDOM when nothing is
 *     armed, which means zero;
 *   - the kernel does not keep the period, so it is kept here and getitimer
 *     returns it while a repeating timer is set;
 *   - times are rounded up to the 60 Hz clock, as libc rounds them.
 *
 * The kept periods are state, one set per process.  A program that calls
 * setitimer itself links libc's copy, and ld exports it when a library refers
 * to it, so the runtime linker binds the library to the program's copy and
 * this one is not used.  Both functions are here together, as in libc, so
 * whichever copy a process uses, it uses both from it.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

extern int __amixrt_syscall(int, ...);

/* <sys/hrtcntl.h> cannot be included: its hrtcmd_t has an ecb_t, which no
 * user header defines.  Its layout and values: */
struct amixrt_hrtime {
	unsigned long secs;
	long rem;
	unsigned long res;
};
struct amixrt_hrtcmd {
	int cmd;
	int clk;
	struct amixrt_hrtime interval;	/* hrtc_int */
	struct amixrt_hrtime tod;	/* hrtc_tod */
	int flags;
	int error;
	long ecb[3];			/* hrtc_ecb, unused here */
};
#define AMIXRT_HRTALARM		1	/* hrtsys subcode */
#define AMIXRT_HRT_BSD		12
#define AMIXRT_HRT_BSD_PEND	13
#define AMIXRT_HRT_BSD_REP	15
#define AMIXRT_HRT_BSD_CANCEL	16
#define AMIXRT_HZ		60
#define AMIXRT_EDOM		33	/* hrtc_error: no alarm pending */

struct amixrt_itimer {
	int repeating;
	struct timeval interval;
};
static struct amixrt_itimer amixrt_itimers[3];

static int amixrt_hrtalarm(struct amixrt_hrtcmd *c)
{
	return __amixrt_syscall(SYS_hrtsys, AMIXRT_HRTALARM, c, 1);
}

static int amixrt_clock(struct amixrt_hrtcmd *c, int which)
{
	static const int clk[3] = { 1, 2, 4 };

	memset(c, 0, sizeof *c);
	if (which < 0 || which > 2) {
		errno = EINVAL;
		return -1;
	}
	c->clk = clk[which];
	return 0;
}

/* A timeval in microseconds, rounded up to a whole clock tick. */
static void amixrt_ticks(const struct timeval *tv, struct amixrt_hrtime *h)
{
	unsigned long t = ((unsigned long)tv->tv_usec * AMIXRT_HZ + 999999) / 1000000;

	h->secs = (unsigned long)tv->tv_sec;
	if (t >= AMIXRT_HZ) {
		h->secs++;
		t = 0;
	}
	h->rem = (long)((t * 1000000 + AMIXRT_HZ - 1) / AMIXRT_HZ);
	h->res = 1000000;
}

/* libc's limits: at most 100000000 seconds, microseconds in range. */
static int amixrt_valid(const struct timeval *tv)
{
	return tv->tv_sec >= 0 && tv->tv_sec <= 100000000L
	       && tv->tv_usec >= 0 && tv->tv_usec <= 999999;
}

static int amixrt_getitimer(int which, struct itimerval *value)
{
	struct amixrt_hrtcmd c;

	if (amixrt_clock(&c, which) != 0) return -1;
	memset(value, 0, sizeof *value);
	c.cmd = AMIXRT_HRT_BSD_PEND;
	if (amixrt_hrtalarm(&c) != 1 && c.error != AMIXRT_EDOM) {
		errno = EINVAL;
		return -1;
	}
	value->it_value.tv_sec = (long)c.interval.secs;
	value->it_value.tv_usec = c.interval.rem;
	if (amixrt_itimers[which].repeating)
		value->it_interval = amixrt_itimers[which].interval;
	return 0;
}

int getitimer(int which, struct itimerval *value)
{
	return amixrt_getitimer(which, value);
}

int setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	struct amixrt_hrtcmd c;
	struct amixrt_itimer *it;
	int repeating, arming;

	if (which < 0 || which > 2 || value == 0 || !amixrt_valid(&value->it_value)
	    || !amixrt_valid(&value->it_interval)) {
		errno = EINVAL;
		return -1;
	}
	if (ovalue != 0 && amixrt_getitimer(which, ovalue) != 0) return -1;

	repeating = value->it_interval.tv_sec != 0 || value->it_interval.tv_usec != 0;
	arming = value->it_value.tv_sec != 0 || value->it_value.tv_usec != 0;
	it = &amixrt_itimers[which];
	it->interval = value->it_interval;

	/* Disarming, or replacing a repeating timer with a one-shot: cancel
	 * the old one first.  A new repeating timer replaces the old by
	 * itself. */
	if (!arming || (it->repeating && !repeating)) {
		it->repeating = 0;
		if (amixrt_clock(&c, which) != 0) return -1;
		c.cmd = AMIXRT_HRT_BSD_CANCEL;
		if (amixrt_hrtalarm(&c) != 1) {
			errno = EINVAL;
			return -1;
		}
		if (!arming) return 0;
	}

	it->repeating = repeating;
	if (amixrt_clock(&c, which) != 0) return -1;
	if (repeating) {
		c.cmd = AMIXRT_HRT_BSD_REP;
		amixrt_ticks(&value->it_interval, &c.interval);
		amixrt_ticks(&value->it_value, &c.tod);
	} else {
		c.cmd = AMIXRT_HRT_BSD;
		amixrt_ticks(&value->it_value, &c.interval);
	}
	if (amixrt_hrtalarm(&c) != 1) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}
