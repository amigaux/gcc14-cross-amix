/* amixrt_select.c -- select(), for libamixrt.so, built on poll(2).
 *
 * SVR4.0's select is a library function over poll, and AMIX has it only as a
 * static member of usr/ccs/lib/libc.so.  This one makes the same
 * translation: a descriptor in readfds is polled for POLLRDNORM, in writefds
 * for POLLWRNORM, in exceptfds for POLLRDBAND; POLLERR and POLLHUP make a
 * descriptor that was asked about readable or writable, as the next read or
 * write will not block; POLLNVAL fails the call with EBADF.  The sets are
 * left empty when poll returns 0 or fails.
 *
 * Two things differ from libc's, both towards POSIX.  The result counts the
 * bits set in the three sets, not the descriptors poll reported, so a
 * descriptor both readable and writable counts twice.  A timeout is rounded
 * up to the next millisecond, so a short wait never becomes no wait at all.
 *
 * Only the descriptors actually named are polled, so the array on the stack
 * is as long as the sets are full rather than FD_SETSIZE entries.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

extern int poll(struct pollfd *, unsigned long, int);

#define AMIXRT_ISSET(n, p) ((p) != 0 && FD_ISSET((n), (p)))

/* poll's timeout in milliseconds: -1 for none, or EINVAL.  libc accepts at
 * most 100000000 seconds and waits at most INT_MAX milliseconds. */
static int amixrt_timeout(const struct timeval *tv, int *ms)
{
	long s, us;

	if (tv == 0) {
		*ms = -1;
		return 0;
	}
	s = tv->tv_sec;
	us = tv->tv_usec;
	if (s < 0 || us < 0 || us >= 1000000 || s > 100000000L) return -1;
	if (s > (INT_MAX - 1000) / 1000) {
		*ms = INT_MAX;
		return 0;
	}
	*ms = (int)(s * 1000 + (us + 999) / 1000);
	return 0;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	   struct timeval *timeout)
{
	int fd, n = 0, i, r, ms, count;

	if (nfds < 0 || nfds > FD_SETSIZE || amixrt_timeout(timeout, &ms) != 0) {
		errno = EINVAL;
		return -1;
	}
	for (fd = 0; fd < nfds; fd++) {
		if (AMIXRT_ISSET(fd, readfds) || AMIXRT_ISSET(fd, writefds)
		    || AMIXRT_ISSET(fd, exceptfds))
			n++;
	}
	{
		struct pollfd pfd[n > 0 ? n : 1];

		i = 0;
		for (fd = 0; fd < nfds && i < n; fd++) {
			short ev = 0;

			if (AMIXRT_ISSET(fd, readfds)) ev |= POLLRDNORM;
			if (AMIXRT_ISSET(fd, writefds)) ev |= POLLWRNORM;
			if (AMIXRT_ISSET(fd, exceptfds)) ev |= POLLRDBAND;
			if (ev == 0) continue;
			pfd[i].fd = fd;
			pfd[i].events = ev;
			pfd[i].revents = 0;
			i++;
		}
		/* An empty array still gets a valid address, for a plain sleep. */
		r = poll(pfd, (unsigned long)n, ms);
		if (r <= 0) {
			if (r < 0) return -1;
			if (readfds != 0) memset(readfds, 0, sizeof *readfds);
			if (writefds != 0) memset(writefds, 0, sizeof *writefds);
			if (exceptfds != 0) memset(exceptfds, 0, sizeof *exceptfds);
			return 0;
		}
		for (i = 0; i < n; i++) {
			if (pfd[i].revents & POLLNVAL) {
				errno = EBADF;
				return -1;
			}
		}
		/* Only now clear the sets: a failure above leaves them as given. */
		if (readfds != 0) memset(readfds, 0, sizeof *readfds);
		if (writefds != 0) memset(writefds, 0, sizeof *writefds);
		if (exceptfds != 0) memset(exceptfds, 0, sizeof *exceptfds);
		count = 0;
		for (i = 0; i < n; i++) {
			short re = pfd[i].revents, ev = pfd[i].events;
			int f = pfd[i].fd;
			int broken = (re & (POLLERR | POLLHUP)) != 0;

			if ((ev & POLLRDNORM) && ((re & POLLRDNORM) || broken)) {
				FD_SET(f, readfds);
				count++;
			}
			if ((ev & POLLWRNORM) && ((re & POLLWRNORM) || broken)) {
				FD_SET(f, writefds);
				count++;
			}
			if ((ev & POLLRDBAND) && (re & POLLRDBAND)) {
				FD_SET(f, exceptfds);
				count++;
			}
		}
		return count;
	}
}
