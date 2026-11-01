/* amixrt_realpath.c -- realpath(), for libamixrt.so.
 *
 * AMIX has realpath only as a static member of usr/ccs/lib/libc.so.  This
 * one resolves the path a component at a time from the root or the current
 * directory, follows each symbolic link where it is found (at most
 * MAXSYMLINKS of them, then ELOOP), and drops "." and "..".  It accepts a
 * null resolved_name, as POSIX.1-2008 does, and then returns a buffer from
 * malloc that the caller frees.  On failure resolved_name holds the part of
 * the path resolved so far, as SVR4 documents.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Remove the last component of an absolute path, keeping the root. */
static void amixrt_up(char *buf)
{
	char *slash = strrchr(buf, '/');

	if (slash == buf) buf[1] = '\0';
	else if (slash != 0) *slash = '\0';
}

/* <stdlib.h> declares the path without const, as SVR4.0 did. */
char *realpath(char *path, char *resolved)
{
	char *buf;
	char left[PATH_MAX], comp[PATH_MAX], link[PATH_MAX];
	size_t len, llen;
	int links = 0;
	struct stat st;

	if (path == 0) {
		errno = EINVAL;
		return 0;
	}
	if (path[0] == '\0') {
		errno = ENOENT;
		return 0;
	}
	if (strlen(path) >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return 0;
	}
	buf = resolved != 0 ? resolved : (char *)malloc(PATH_MAX);
	if (buf == 0) {
		errno = ENOMEM;
		return 0;
	}
	if (path[0] == '/') {
		strcpy(buf, "/");
		strcpy(left, path + 1);
	} else {
		if (getcwd(buf, PATH_MAX) == 0) goto fail;
		strcpy(left, path);
	}

	while (left[0] != '\0') {
		char *p = strchr(left, '/');

		/* Take the first component off the front of left. */
		if (p != 0) {
			len = (size_t)(p - left);
			memcpy(comp, left, len);
			comp[len] = '\0';
			while (*p == '/') p++;
			memmove(left, p, strlen(p) + 1);
		} else {
			strcpy(comp, left);
			left[0] = '\0';
		}
		if (comp[0] == '\0' || strcmp(comp, ".") == 0) continue;
		if (strcmp(comp, "..") == 0) {
			amixrt_up(buf);
			continue;
		}

		len = strlen(buf);
		if (len + (buf[len - 1] != '/') + strlen(comp) >= PATH_MAX) {
			errno = ENAMETOOLONG;
			goto fail;
		}
		if (buf[len - 1] != '/') strcat(buf, "/");
		strcat(buf, comp);

		if (lstat(buf, &st) != 0) goto fail;
		if ((st.st_mode & S_IFMT) == S_IFLNK) {
			int n;

			if (++links > MAXSYMLINKS) {
				errno = ELOOP;
				goto fail;
			}
			n = readlink(buf, link, sizeof link - 1);
			if (n < 0) goto fail;
			link[n] = '\0';
			/* The link replaces this component, so the rest of the
			 * path is resolved against what it names. */
			if (link[0] == '/') strcpy(buf, "/");
			else amixrt_up(buf);
			llen = strlen(link);
			if (left[0] != '\0') {
				if (llen + 1 + strlen(left) >= PATH_MAX) {
					errno = ENAMETOOLONG;
					goto fail;
				}
				link[llen] = '/';
				strcpy(link + llen + 1, left);
			}
			strcpy(left, link[0] == '/' ? link + 1 : link);
		} else if ((st.st_mode & S_IFMT) != S_IFDIR && left[0] != '\0') {
			errno = ENOTDIR;
			goto fail;
		}
	}
	return buf;

fail:
	if (resolved == 0) free(buf);
	return 0;
}
