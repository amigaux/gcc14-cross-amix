/* amixrt_truncate.c -- truncate and ftruncate, for libamixrt.so.
 *
 * SVR4.0 has no truncate system call.  Its libc frees the file's space from
 * the new length on with fcntl(F_FREESP), and AMIX has that only as a static
 * member of usr/ccs/lib/libc.so.  Same method here: a lock-style range that
 * starts at the length and runs to the end of the file (l_len 0).
 */
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

int ftruncate(int fd, off_t length)
{
	struct flock fl;

	fl.l_type = F_WRLCK;
	fl.l_whence = 0;
	fl.l_start = length;
	fl.l_len = 0;
	fl.l_sysid = 0;
	fl.l_pid = 0;
	return fcntl(fd, F_FREESP, &fl);
}

int truncate(const char *path, off_t length)
{
	int fd, r, saved;

	if ((fd = open(path, O_WRONLY)) < 0) return -1;
	r = ftruncate(fd, length);
	saved = errno;
	close(fd);
	errno = saved;
	return r;
}
