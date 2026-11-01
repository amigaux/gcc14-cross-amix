/* amixrt-host.c -- test libamixrt's portable parts on the build host.
 *
 * realpath and select are ordinary POSIX code over lstat, readlink, getcwd
 * and poll, so their logic can be checked natively in a second.  Each is
 * compiled in under a test name, after the host's own headers, so the host's
 * realpath and select stay what the comparisons are made against.
 * tools/test-compat-host.sh builds and runs it in a scratch directory.
 */
#define _DEFAULT_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define realpath amixrt_test_realpath
#include "../compat/amixrt_realpath.c"
#undef realpath
#define select amixrt_test_select
#include "../compat/amixrt_select.c"
#undef select

static int failures;

static void check(const char *what, int ok)
{
	printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok) failures++;
}

/* The test realpath against the host's, on success and on failure. */
static void same_realpath(const char *path)
{
	char a[PATH_MAX], b[PATH_MAX], what[PATH_MAX + 32];
	char *ra, *rb;
	int ea, eb;

	errno = 0;
	ra = amixrt_test_realpath((char *)path, a);
	ea = errno;
	errno = 0;
	rb = realpath(path, b);
	eb = errno;
	snprintf(what, sizeof what, "realpath(\"%s\")", path);
	if (rb != 0) check(what, ra != 0 && strcmp(a, b) == 0);
	else check(what, ra == 0 && ea == eb);
	if ((ra != 0) != (rb != 0) || (ra && strcmp(a, b) != 0))
		printf("    got %s (errno %d), host %s (errno %d)\n",
		       ra ? a : "NULL", ea, rb ? b : "NULL", eb);
}

static void test_realpath(void)
{
	char *m;

	/* A tree in the current directory, which the script makes empty. */
	if (mkdir("d", 0755) || mkdir("d/e", 0755) || symlink("d/e", "le")
	    || symlink("../e", "d/e/up") || symlink("/", "root")
	    || symlink("loop2", "loop1") || symlink("loop1", "loop2")
	    || symlink("missing", "dangling")) {
		perror("amixrt-host: making the tree");
		exit(2);
	}
	if (close(open("d/f", O_CREAT | O_WRONLY, 0644)) != 0) exit(2);
	same_realpath(".");
	same_realpath("d");
	same_realpath("./d/../d/e/");
	same_realpath("le");
	same_realpath("le/up/up/..");
	same_realpath("root");
	same_realpath("root/tmp/..");
	same_realpath("/");
	same_realpath("//d");
	same_realpath("d/f");
	same_realpath("d/f/x");
	same_realpath("nothere");
	same_realpath("dangling");
	same_realpath("loop1");
	same_realpath("");
	m = amixrt_test_realpath("d/e", 0);
	check("realpath(path, NULL) returns a malloc'd name",
	      m != 0 && strcmp(m + strlen(m) - 4, "/d/e") == 0);
	free(m);
}

static void test_select(void)
{
	int p[2], q[2], r;
	fd_set rd, wr, ex;
	struct timeval tv;

	if (pipe(p) || pipe(q)) exit(2);
	FD_ZERO(&rd); FD_ZERO(&wr); FD_ZERO(&ex);
	FD_SET(p[0], &rd); FD_SET(p[1], &wr);
	tv.tv_sec = 0; tv.tv_usec = 0;
	r = amixrt_test_select(FD_SETSIZE, &rd, &wr, &ex, &tv);
	check("select: empty pipe not readable, its write end writable",
	      r == 1 && !FD_ISSET(p[0], &rd) && FD_ISSET(p[1], &wr));

	if (write(p[1], "x", 1) != 1) exit(2);
	FD_ZERO(&rd); FD_ZERO(&wr);
	FD_SET(p[0], &rd); FD_SET(q[0], &rd); FD_SET(p[1], &wr);
	r = amixrt_test_select(FD_SETSIZE, &rd, &wr, 0, &tv);
	check("select: counts bits, one readable and one writable",
	      r == 2 && FD_ISSET(p[0], &rd) && !FD_ISSET(q[0], &rd)
	      && FD_ISSET(p[1], &wr));

	FD_ZERO(&rd);
	FD_SET(q[0], &rd);
	tv.tv_sec = 0; tv.tv_usec = 20000;
	r = amixrt_test_select(q[0] + 1, &rd, 0, 0, &tv);
	check("select: times out with 0 and an empty set",
	      r == 0 && !FD_ISSET(q[0], &rd));

	close(q[1]);
	FD_ZERO(&rd);
	FD_SET(q[0], &rd);
	r = amixrt_test_select(q[0] + 1, &rd, 0, 0, 0);
	check("select: a hung-up pipe is readable", r == 1 && FD_ISSET(q[0], &rd));

	close(q[0]);
	FD_ZERO(&rd);
	FD_SET(q[0], &rd);
	errno = 0;
	r = amixrt_test_select(q[0] + 1, &rd, 0, 0, &tv);
	check("select: a closed descriptor fails with EBADF, set untouched",
	      r == -1 && errno == EBADF && FD_ISSET(q[0], &rd));

	tv.tv_sec = 0; tv.tv_usec = 1000000;
	errno = 0;
	r = amixrt_test_select(0, 0, 0, 0, &tv);
	check("select: tv_usec out of range is EINVAL", r == -1 && errno == EINVAL);
	errno = 0;
	r = amixrt_test_select(-1, 0, 0, 0, 0);
	check("select: negative nfds is EINVAL", r == -1 && errno == EINVAL);
	tv.tv_sec = 0; tv.tv_usec = 30000;
	{
		struct timeval t0, t1;
		long us;

		gettimeofday(&t0, 0);
		r = amixrt_test_select(0, 0, 0, 0, &tv);
		gettimeofday(&t1, 0);
		us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);
		check("select: no descriptors sleeps for the timeout",
		      r == 0 && us >= 29000);
	}
	tv.tv_sec = 0; tv.tv_usec = 1;
	{
		int ms = -2;

		check("select: 1 us rounds up to 1 ms, not to no wait",
		      amixrt_timeout(&tv, &ms) == 0 && ms == 1);
	}
	close(p[0]);
	close(p[1]);
}

int main(void)
{
	test_realpath();
	test_select();
	if (failures) {
		printf("amixrt-host: %d FAILED\n", failures);
		return 1;
	}
	printf("amixrt-host: all passed\n");
	return 0;
}
