/* amixrt_getutx.c -- the utmpx functions, for libamixrt.so.
 *
 * libc.so.1 exports none of them; AMIX has them only as a static member of
 * usr/ccs/lib/libc.so, getutx.o, which keeps the open utmpx file, the
 * current entry and the two file names between calls.  CDE's terminal
 * library records its sessions with setutxent, getutxline, pututxline and
 * endutxent.  This file has every function of the member that shares that
 * state -- getutxent, getutxid, getutxline, pututxline, setutxent,
 * endutxent, utmpxname and updutmp -- so that a program linking libc's
 * member gets all of them from the program and a library none from here.
 * It leaves out makeutx and modutx, which allocate inittab ids and report to
 * init through /etc/initpipe, and updwtmpx, which shares nothing: no library
 * calls them.
 *
 * Written from <utmpx.h>, <utmp.h> and the documented SVR4 behaviour, and
 * behaving as libc's member does (read off its code), files and all:
 *
 *  - /var/adm/utmpx holds struct utmpx records, 372 bytes, and is kept in
 *    step with /var/adm/utmp, struct utmp records of 36.  The first
 *    getutxent opens utmpx read-write, creating it 0644, or else read-only;
 *    creates utmp if it is missing; and brings the two files into step:
 *    when one is empty the other is converted into it, when their times
 *    differ by 10 seconds or more the newer one is, and a file whose size is
 *    not a whole number of records fails with EINVAL.  Converting truncates
 *    the older file, rewrites it from the newer, and sets the newer one's
 *    time to now.  Each getutxent reads the next record into a buffer of its
 *    own and returns it, or clears it and returns a null pointer.
 *  - getutxid searches from the current entry on: an EMPTY id finds
 *    nothing; RUN_LVL, BOOT_TIME, OLD_TIME and NEW_TIME find the next entry
 *    of that type; INIT_PROCESS to DEAD_PROCESS find the next entry of one of
 *    those four types with the same ut_id.  getutxline finds the next
 *    LOGIN_PROCESS or USER_PROCESS entry with the same ut_line.
 *  - pututxline reads one entry on, writes over the entry getutxid finds
 *    for the new one (looking from there, then from the start), or appends,
 *    and then updutmp writes the same entry, converted, into utmp, where
 *    the search is getutxid's except that an entry of a time type also
 *    matches a process entry with the same ut_id (libc's falls through
 *    there).  If utmp cannot be written, the utmpx entry is put back and
 *    pututxline fails.  It needs utmpx open for writing: root's, or a
 *    group-writable file's group.
 *  - utmpxname takes a name of at most 78 characters ending in 'x', names
 *    utmp the same without the 'x', and closes the files.
 *
 * Differences, all where libc's member reads or writes what it never set:
 * libc's utmpxname reads the byte before an empty name; this one refuses
 * it.  When pututxline's write fails and utmp then cannot be written, libc's
 * writes an uninitialised buffer back into utmpx; this one writes back only
 * an entry it overwrote.  A record converted from utmp has ut_syslen 0,
 * where libc's leaves whatever its stack held.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <utmp.h>
#include <utmpx.h>

/* The record sizes and offsets of the files on disk. */
_Static_assert(sizeof(struct utmp) == 36, "struct utmp");
_Static_assert(offsetof(struct utmp, ut_type) == 26, "utmp ut_type");
_Static_assert(offsetof(struct utmp, ut_time) == 32, "utmp ut_time");
_Static_assert(sizeof(struct utmpx) == 372, "struct utmpx");
_Static_assert(offsetof(struct utmpx, ut_pid) == 68, "utmpx ut_pid");
_Static_assert(offsetof(struct utmpx, ut_type) == 72, "utmpx ut_type");
_Static_assert(offsetof(struct utmpx, ut_tv) == 80, "utmpx ut_tv");
_Static_assert(offsetof(struct utmpx, ut_host) == 114, "utmpx ut_host");

#define AMIXRT_UTNAME	80	/* libc's buffers for the two names */

int updutmp(const struct utmpx *);

static int amixrt_utxfd = -1;
static char amixrt_utxname[AMIXRT_UTNAME] = UTMPX_FILE;
static char amixrt_utname[AMIXRT_UTNAME] = UTMP_FILE;
static struct utmpx amixrt_utx;

/* The conversions copy names with strncpy into utmp's shorter fields, cut
 * and unterminated when they are full, as the file format has them. */
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#pragma GCC diagnostic ignored "-Wsizeof-pointer-memaccess"

static void amixrt_getutmp(const struct utmpx *x, struct utmp *u)
{
	strncpy(u->ut_user, x->ut_user, sizeof u->ut_user);
	strncpy(u->ut_line, x->ut_line, sizeof u->ut_line);
	memcpy(u->ut_id, x->ut_id, sizeof u->ut_id);
	u->ut_pid = (short)x->ut_pid;
	u->ut_type = x->ut_type;
	u->ut_exit = x->ut_exit;
	u->ut_time = x->ut_tv.tv_sec;
}

static void amixrt_getutmpx(const struct utmp *u, struct utmpx *x)
{
	memset(x, 0, sizeof *x);
	strncpy(x->ut_user, u->ut_user, sizeof u->ut_user);
	strncpy(x->ut_line, u->ut_line, sizeof u->ut_line);
	memcpy(x->ut_id, u->ut_id, sizeof u->ut_id);
	x->ut_pid = u->ut_pid;
	x->ut_type = u->ut_type;
	x->ut_exit = u->ut_exit;
	x->ut_tv.tv_sec = u->ut_time;
}

/* Rewrite utmp from utmpx, then give utmpx the time of now. */
static int amixrt_utfromx(const char *utmp, const char *utmpx)
{
	struct utmpx x;
	struct utmp u;
	int ufd, xfd;

	if ((ufd = open(utmp, O_RDWR | O_TRUNC)) < 0) return 1;
	if ((xfd = open(utmpx, O_RDONLY)) < 0) {
		close(ufd);
		return 1;
	}
	while (read(xfd, &x, sizeof x) == sizeof x) {
		amixrt_getutmp(&x, &u);
		if (write(ufd, &u, sizeof u) != sizeof u) {
			close(ufd);
			close(xfd);
			return 1;
		}
	}
	close(ufd);
	close(xfd);
	utime(utmpx, 0);
	return 0;
}

/* Rewrite utmpx from utmp, then give utmp the time of now. */
static int amixrt_utxfromu(const char *utmp, const char *utmpx)
{
	struct utmpx x;
	struct utmp u;
	int ufd, xfd;

	if ((ufd = open(utmp, O_RDONLY)) < 0) return 1;
	if ((xfd = open(utmpx, O_RDWR | O_TRUNC)) < 0) {
		close(ufd);
		return 1;
	}
	while (read(ufd, &u, sizeof u) == sizeof u) {
		amixrt_getutmpx(&u, &x);
		if (write(xfd, &x, sizeof x) != sizeof x) {
			close(ufd);
			close(xfd);
			return 1;
		}
	}
	close(ufd);
	close(xfd);
	utime(utmp, 0);
	return 0;
}

/* Bring utmp and utmpx into step; 0 when they are. */
static int amixrt_synchutmp(const char *utmp, const char *utmpx)
{
	struct stat su, sx;
	long d;

	if (stat(utmp, &su) != 0 || stat(utmpx, &sx) != 0) return 1;
	if ((unsigned long)su.st_size % sizeof(struct utmp) != 0
	    || (unsigned long)sx.st_size % sizeof(struct utmpx) != 0) {
		errno = EINVAL;
		return 1;
	}
	if (su.st_size != 0 && sx.st_size == 0) return amixrt_utxfromu(utmp, utmpx);
	if (su.st_size == 0 && sx.st_size != 0) return amixrt_utfromx(utmp, utmpx);
	d = (long)sx.st_mtime - (long)su.st_mtime;
	if (d >= -9 && d <= 9) return 0;
	if (sx.st_mtime > su.st_mtime) return amixrt_utfromx(utmp, utmpx);
	return amixrt_utxfromu(utmp, utmpx);
}

struct utmpx *getutxent(void)
{
	if (amixrt_utxfd < 0) {
		amixrt_utxfd = open(amixrt_utxname, O_RDWR | O_CREAT, 0644);
		if (amixrt_utxfd < 0) {
			amixrt_utxfd = open(amixrt_utxname, O_RDONLY);
			if (amixrt_utxfd < 0) return 0;
		}
		if (access(amixrt_utname, F_OK) < 0) {
			int fd = open(amixrt_utname, O_RDWR | O_CREAT, 0644);

			if (fd < 0) return 0;
			close(fd);
		}
		if (amixrt_synchutmp(amixrt_utname, amixrt_utxname) != 0) return 0;
	}
	if (read(amixrt_utxfd, &amixrt_utx, sizeof amixrt_utx) != sizeof amixrt_utx) {
		memset(&amixrt_utx, 0, sizeof amixrt_utx);
		return 0;
	}
	return &amixrt_utx;
}

static int amixrt_utxproc(int type)
{
	return type == INIT_PROCESS || type == LOGIN_PROCESS || type == USER_PROCESS
	       || type == DEAD_PROCESS;
}

struct utmpx *getutxid(const struct utmpx *ut)
{
	do {
		if (amixrt_utx.ut_type == EMPTY) continue;
		if (ut->ut_type < EMPTY || ut->ut_type > DEAD_PROCESS) return 0;
		switch (ut->ut_type) {
		case EMPTY:
			return 0;
		case RUN_LVL:
		case BOOT_TIME:
		case OLD_TIME:
		case NEW_TIME:
			if (amixrt_utx.ut_type == ut->ut_type) return &amixrt_utx;
			break;
		default:
			if (amixrt_utxproc(amixrt_utx.ut_type)
			    && memcmp(amixrt_utx.ut_id, ut->ut_id, sizeof ut->ut_id) == 0)
				return &amixrt_utx;
			break;
		}
	} while (getutxent() != 0);
	return 0;
}

struct utmpx *getutxline(const struct utmpx *ut)
{
	do {
		if ((amixrt_utx.ut_type == LOGIN_PROCESS || amixrt_utx.ut_type == USER_PROCESS)
		    && strncmp(ut->ut_line, amixrt_utx.ut_line, sizeof ut->ut_line) == 0)
			return &amixrt_utx;
	} while (getutxent() != 0);
	return 0;
}

struct utmpx *pututxline(const struct utmpx *ut)
{
	struct utmpx entry, old;
	struct utmpx *r;
	int fl, wrote;

	if (ut == 0) return 0;
	memcpy(&entry, ut, sizeof entry);
	getutxent();
	if (amixrt_utxfd < 0) return 0;
	fl = fcntl(amixrt_utxfd, F_GETFL, 0);
	if (fl == -1 || (fl & O_RDWR) != O_RDWR) return 0;
	if (getutxid(&entry) == 0) {
		setutxent();
		if (getutxid(&entry) == 0) {
			fcntl(amixrt_utxfd, F_SETFL, fl | O_APPEND);
			goto put;
		}
	}
	lseek(amixrt_utxfd, -(off_t)sizeof entry, SEEK_CUR);
put:
	wrote = write(amixrt_utxfd, &entry, sizeof entry) == sizeof entry;
	if (wrote) {
		memcpy(&old, &amixrt_utx, sizeof old);
		memcpy(&amixrt_utx, &entry, sizeof amixrt_utx);
		r = &amixrt_utx;
	} else {
		r = 0;
	}
	if (updutmp(ut) != 0) {
		if (wrote) {
			lseek(amixrt_utxfd, -(off_t)sizeof old, SEEK_CUR);
			write(amixrt_utxfd, &old, sizeof old);
		}
		r = 0;
	}
	fcntl(amixrt_utxfd, F_SETFL, fl);
	return r;
}

void setutxent(void)
{
	if (amixrt_utxfd != -1) lseek(amixrt_utxfd, 0, SEEK_SET);
	memset(&amixrt_utx, 0, sizeof amixrt_utx);
}

void endutxent(void)
{
	if (amixrt_utxfd != -1) close(amixrt_utxfd);
	amixrt_utxfd = -1;
	memset(&amixrt_utx, 0, sizeof amixrt_utx);
}

int utmpxname(const char *name)
{
	size_t len = strlen(name);

	if (len == 0 || len > AMIXRT_UTNAME - 2 || name[len - 1] != 'x') return 0;
	strcpy(amixrt_utxname, name);
	strcpy(amixrt_utname, name);
	amixrt_utname[len - 1] = 0;
	endutxent();
	return 1;
}

/* Write the utmpx entry, converted, into utmp: over the entry it matches,
 * else at the end.  0 on success, 1 on failure. */
int updutmp(const struct utmpx *utx)
{
	struct utmp u;
	int fd, fl, found = 0;

	if ((fd = open(amixrt_utname, O_RDWR | O_CREAT, 0644)) < 0) return 1;
	if ((fl = fcntl(fd, F_GETFL, 0)) == -1) {
		close(fd);
		return 1;
	}
	while (!found && read(fd, &u, sizeof u) == sizeof u) {
		if (u.ut_type == EMPTY) continue;
		if (utx->ut_type < EMPTY || utx->ut_type > DEAD_PROCESS) continue;
		if (utx->ut_type == EMPTY) break;
		if (utx->ut_type <= NEW_TIME && u.ut_type == utx->ut_type) found = 1;
		else if (amixrt_utxproc(u.ut_type) && memcmp(u.ut_id, utx->ut_id, sizeof u.ut_id) == 0)
			found = 1;
	}
	if (found) lseek(fd, -(off_t)sizeof u, SEEK_CUR);
	else fcntl(fd, F_SETFL, fl | O_APPEND);
	amixrt_getutmp(utx, &u);
	if (write(fd, &u, sizeof u) != sizeof u) {
		close(fd);
		return 1;
	}
	fcntl(fd, F_SETFL, fl);
	close(fd);
	return 0;
}
