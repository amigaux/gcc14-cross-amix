/* amixrt-libc-host.c -- test libamixrt's getmntent, syslog and utmpx groups
 * on the build host.
 *
 * Their logic is ordinary C over open, read, write, fgets and strtok, so it
 * can be checked natively in a second.  Each file is compiled in under test
 * names, after the host's own headers; tests/host-shim supplies <utmp.h>,
 * <utmpx.h>, <sys/mnttab.h>, <sys/strlog.h> and <stropts.h> with AMIX's record
 * layouts, so the files written here have AMIX's 36- and 372-byte records.
 * putmsg is this file's: it keeps what syslog sends.  The expected results
 * are libc's members' behaviour as the comments in compat/ describe it; the
 * same calls are compared with libc's own on AMIX by probe/libc-calls.c.
 * tools/test-compat-host.sh builds and runs it in a scratch directory.
 */
#define _DEFAULT_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <stropts.h>
#include <sys/strlog.h>
#include <sys/mnttab.h>
#include <utmp.h>
#include <utmpx.h>

#define getmntent t_getmntent
#define getmntany t_getmntany
#include "../compat/amixrt_getmntent.c"
#define openlog t_openlog
#define syslog t_syslog
#define vsyslog t_vsyslog
#define closelog t_closelog
#define setlogmask t_setlogmask
#include "../compat/amixrt_syslog.c"
#define getutxent t_getutxent
#define getutxid t_getutxid
#define getutxline t_getutxline
#define pututxline t_pututxline
#define setutxent t_setutxent
#define endutxent t_endutxent
#define utmpxname t_utmpxname
#define updutmp t_updutmp
/* <utmpx.h> declared them under their own names, before the renames. */
void setutxent(void);
struct utmpx *getutxent(void);
#include "../compat/amixrt_getutx.c"

static int failures;

static void check(const char *what, int ok)
{
	printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok) failures++;
}

static int same(const char *a, const char *b)
{
	return a == b || (a && b && strcmp(a, b) == 0);
}

/* ---- getmntent, getmntany */

static int mnt_is(const struct mnttab *m, const char *s, const char *p, const char *t,
		  const char *o, const char *tm)
{
	return same(m->mnt_special, s) && same(m->mnt_mountp, p) && same(m->mnt_fstype, t)
	       && same(m->mnt_mntopts, o) && same(m->mnt_time, tm);
}

static void test_mnttab(void)
{
	char longline[1100];
	struct mnttab m, ref;
	FILE *fp = fopen("mnttab", "w");

	fprintf(fp, "# a comment\n\n   \t# an indented comment\n");
	fprintf(fp, "/dev/dsk/c0d0s0 / ufs rw,suid 690000000\n");
	fprintf(fp, "/proc\t/proc\tproc\t-\t690000001\n");
	fprintf(fp, "- /mnt - rw -\n");
	fprintf(fp, "too few fields\n");
	fprintf(fp, "one two three four five six\n");
	memset(longline, 'x', sizeof longline);
	longline[sizeof longline - 2] = '\n';
	longline[sizeof longline - 1] = 0;
	fputs(longline, fp);
	fprintf(fp, "/dev/dsk/c0d0s2 /usr ufs rw 690000002\n");
	fprintf(fp, "/dev/./null /nulldev nullfs rw 690000003\n");
	fclose(fp);

	fp = fopen("mnttab", "r");
	check("getmntent: skips comments, splits on blanks and tabs",
	      t_getmntent(fp, &m) == 0 && mnt_is(&m, "/dev/dsk/c0d0s0", "/", "ufs", "rw,suid", "690000000"));
	check("getmntent: '-' reads as a null pointer",
	      t_getmntent(fp, &m) == 0 && mnt_is(&m, "/proc", "/proc", "proc", 0, "690000001"));
	check("getmntent: '-' in every place",
	      t_getmntent(fp, &m) == 0 && mnt_is(&m, 0, "/mnt", 0, "rw", 0));
	check("getmntent: three fields are MNT_TOOFEW", t_getmntent(fp, &m) == MNT_TOOFEW);
	check("getmntent: six fields are MNT_TOOMANY, five filled in",
	      t_getmntent(fp, &m) == MNT_TOOMANY && mnt_is(&m, "one", "two", "three", "four", "five"));
	check("getmntent: 1023 characters and no newline are MNT_TOOLONG",
	      t_getmntent(fp, &m) == MNT_TOOLONG);
	check("getmntent: the rest of that line is read as the next",
	      t_getmntent(fp, &m) == MNT_TOOFEW);
	check("getmntent: and on", t_getmntent(fp, &m) == 0 && same(m.mnt_mountp, "/usr"));
	check("getmntent: the last line", t_getmntent(fp, &m) == 0 && same(m.mnt_mountp, "/nulldev"));
	check("getmntent: -1 at the end", t_getmntent(fp, &m) == -1);
	fclose(fp);

	/* getmntany stops at the first line getmntent does not return 0 for,
	 * so the matches are looked for in a file of good lines. */
	fp = fopen("mnttab.good", "w");
	fprintf(fp, "# good lines only\n/dev/dsk/c0d0s0 / ufs rw,suid 690000000\n");
	fprintf(fp, "/proc\t/proc\tproc\t-\t690000001\n");
	fprintf(fp, "/dev/dsk/c0d0s2 /usr ufs rw 690000002\n");
	fprintf(fp, "/dev/./null /nulldev nullfs rw 690000003\n");
	fclose(fp);
	memset(&ref, 0, sizeof ref);
	ref.mnt_mountp = "/usr";
	fp = fopen("mnttab.good", "r");
	check("getmntany: by mount point", t_getmntany(fp, &m, &ref) == 0
	      && same(m.mnt_special, "/dev/dsk/c0d0s2"));
	fclose(fp);
	memset(&ref, 0, sizeof ref);
	ref.mnt_fstype = "proc";
	ref.mnt_time = "690000001";
	fp = fopen("mnttab.good", "r");
	check("getmntany: by two fields", t_getmntany(fp, &m, &ref) == 0 && same(m.mnt_special, "/proc"));
	fclose(fp);
	memset(&ref, 0, sizeof ref);
	ref.mnt_special = "/dev/null";
	fp = fopen("mnttab.good", "r");
	check("getmntany: a device special matches the same device by rdev",
	      t_getmntany(fp, &m, &ref) == 0 && same(m.mnt_mountp, "/nulldev"));
	fclose(fp);
	memset(&ref, 0, sizeof ref);
	ref.mnt_special = "/proc";
	ref.mnt_mountp = "/nowhere";
	fp = fopen("mnttab", "r");
	check("getmntany: stops at the first line that is not an entry (MNT_TOOFEW)",
	      t_getmntany(fp, &m, &ref) == MNT_TOOFEW);
	fclose(fp);
	fp = fopen("mnttab.good", "r");
	check("getmntany: -1 when nothing matches", t_getmntany(fp, &m, &ref) == -1);
	fclose(fp);
	unlink("mnttab");
	unlink("mnttab.good");
}

/* ---- syslog */

static int sent, sent_len, sent_pri, sent_flags, sent_level, ctl_len, data_max, fail_next;
static char sent_text[2048];

int putmsg(int fd, const struct strbuf *ctl, const struct strbuf *data, int flags)
{
	const struct log_ctl *lc = (const struct log_ctl *)ctl->buf;

	(void)fd;
	(void)flags;
	sent++;
	ctl_len = ctl->len;
	sent_pri = lc->pri;
	sent_flags = lc->flags;
	sent_level = lc->level;
	sent_len = data->len;
	data_max = data->maxlen;
	memcpy(sent_text, data->buf, data->len);
	sent_text[data->len] = 0;
	if (fail_next) {
		fail_next = 0;
		return -1;
	}
	return 0;
}

static void test_syslog(void)
{
	char want[256], big[2100];
	int n;

	sent = 0;
	t_syslog(LOG_ERR, "before openlog");
	check("syslog: nothing is sent before openlog", sent == 0);
	check("setlogmask: 0 before openlog, and no state touched", t_setlogmask(0x7f) == 0);

	t_openlog("probe", LOG_PID, LOG_LOCAL0);
	check("setlogmask: every priority after openlog", t_setlogmask(LOG_UPTO(LOG_INFO)) == 0xff);
	check("setlogmask: 0 leaves the mask", t_setlogmask(0) == LOG_UPTO(LOG_INFO));
	errno = ENOENT;
	t_syslog(LOG_INFO, "a %s %d: %m, 100%%\nnot sent", "b", 7);
	sprintf(want, "probe[%d]: a b 7: %s, 100%%\n", (int)getpid(), strerror(ENOENT));
	check("syslog: sent once", sent == 1);
	check("syslog: a 24-byte log_ctl, level 0, SL_CONSOLE",
	      ctl_len == 24 && sent_level == 0 && sent_flags == SL_CONSOLE);
	check("syslog: the openlog facility added to the priority", sent_pri == (LOG_LOCAL0 | LOG_INFO));
	check("syslog: the time, ' ', then ident[pid]: and the text to its newline",
	      sent_text[15] == ' ' && strcmp(sent_text + 16, want) == 0);
	check("syslog: the length counts the null; maxlen 1025",
	      sent_len == (int)strlen(sent_text) + 1 && data_max == 1025);
	n = sent;
	t_syslog(LOG_DEBUG, "masked");
	t_syslog(0, "priority 0");
	t_syslog(LOG_ERR | (24 << 3), "facility 24");
	check("syslog: a masked priority, priority 0 and facility 24 are dropped", sent == n);
	t_syslog(LOG_ERR | LOG_MAIL, "mail");
	check("syslog: a priority's own facility is kept", sent == n + 1 && sent_pri == (LOG_MAIL | LOG_ERR));
	memset(big, 'y', sizeof big);
	big[sizeof big - 1] = 0;
	t_syslog(LOG_ERR, "%s", big);
	check("syslog: at most 1024 bytes are sent", sent_len == 1024);
	t_closelog();
	t_openlog(0, 0, 0);
	t_syslog(LOG_WARNING, "x");
	check("openlog(0, 0, 0): ident and facility kept, no pid",
	      strcmp(sent_text + 16, "probe: x\n") == 0 && sent_pri == (LOG_LOCAL0 | LOG_WARNING));
	t_openlog("cons", LOG_CONS | LOG_NOWAIT, 0);
	fail_next = 1;
	n = sent;
	t_syslog(LOG_ERR, "putmsg fails, LOG_CONS");
	check("syslog: LOG_CONS falls back to a child and returns", sent == n + 1);
	t_closelog();
}

/* ---- the utmpx group */

static void utx(struct utmpx *u, short type, const char *id, const char *line, const char *user,
		long pid, long t)
{
	memset(u, 0, sizeof *u);
	u->ut_type = type;
	strncpy(u->ut_id, id, sizeof u->ut_id);
	strncpy(u->ut_line, line, sizeof u->ut_line);
	strncpy(u->ut_user, user, sizeof u->ut_user);
	u->ut_pid = pid;
	u->ut_tv.tv_sec = t;
	u->ut_tv.tv_usec = 250;
	u->ut_session = 3;
	strcpy(u->ut_host, "host.example");
	u->ut_syslen = 13;
}

static long fsize(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static int readrec(const char *p, int i, void *buf, int size)
{
	int fd = open(p, O_RDONLY), ok;

	ok = fd >= 0 && pread(fd, buf, size, (off_t)i * size) == size;
	if (fd >= 0) close(fd);
	return ok;
}

static void settime(const char *p, long t)
{
	struct utimbuf tb;

	tb.actime = t;
	tb.modtime = t;
	utime(p, &tb);
}

static void test_utmpx(void)
{
	struct utmpx e, x, *p;
	struct utmp u, ue;
	int fd, n;

	check("utmpxname: refuses a name not ending in x", t_utmpxname("t.utmp") == 0);
	check("utmpxname: refuses an empty name", t_utmpxname("") == 0);
	check("utmpxname: refuses 79 characters",
	      t_utmpxname("0123456789012345678901234567890123456789012345678901234567890123456789012345678x") == 0);
	check("utmpxname: takes a name ending in x", t_utmpxname("t.utmpx") == 1);
	check("getutxent: new files give no entry", t_getutxent() == 0);
	check("getutxent: and creates both, empty", fsize("t.utmpx") == 0 && fsize("t.utmp") == 0);

	utx(&e, USER_PROCESS, "p1", "ttyp1", "probe", 1234, 900000000);
	p = t_pututxline(&e);
	check("pututxline: appends p1", p != 0 && memcmp(p, &e, sizeof e) == 0);
	utx(&e, LOGIN_PROCESS, "p2", "ttyp2", "LOGIN", 1235, 900000010);
	check("pututxline: appends p2", t_pututxline(&e) != 0);
	utx(&e, BOOT_TIME, "", "system boot", "", 0, 899999000);
	check("pututxline: appends a boot record", t_pututxline(&e) != 0);
	utx(&e, DEAD_PROCESS, "p1", "ttyp1", "probe", 1234, 900000020);
	e.ut_exit.e_termination = 1;
	e.ut_exit.e_exit = 2;
	check("pututxline: p1 again, as DEAD_PROCESS", t_pututxline(&e) != 0);
	check("utmpx: three records, utmp three", fsize("t.utmpx") == 3 * 372 && fsize("t.utmp") == 3 * 36);
	check("utmpx: p1's record overwritten in place",
	      readrec("t.utmpx", 0, &x, sizeof x) && memcmp(&x, &e, sizeof e) == 0);
	check("utmp: p1's record converted",
	      readrec("t.utmp", 0, &u, sizeof u) && u.ut_type == DEAD_PROCESS && memcmp(u.ut_id, "p1\0\0", 4) == 0
	      && memcmp(u.ut_user, "probe\0\0\0", 8) == 0 && strcmp(u.ut_line, "ttyp1") == 0
	      && u.ut_pid == 1234 && u.ut_exit.e_termination == 1 && u.ut_exit.e_exit == 2
	      && u.ut_time == 900000020);
	check("utmp: the boot record third",
	      readrec("t.utmp", 2, &u, sizeof u) && u.ut_type == BOOT_TIME && u.ut_time == 899999000
	      && memcmp(u.ut_line, "system boot\0", 12) == 0);

	t_setutxent();
	utx(&e, USER_PROCESS, "", "ttyp2", "", 0, 0);
	p = t_getutxline(&e);
	check("getutxline: a LOGIN_PROCESS on the line", p != 0 && strcmp(p->ut_id, "p2") == 0);
	check("getutxline: the current entry is looked at first", t_getutxline(&e) == p);
	t_setutxent();
	utx(&e, USER_PROCESS, "", "ttyp1", "", 0, 0);
	check("getutxline: not a DEAD_PROCESS", t_getutxline(&e) == 0);
	t_setutxent();
	utx(&e, INIT_PROCESS, "p1", "", "", 0, 0);
	p = t_getutxid(&e);
	check("getutxid: any process type with the same id", p != 0 && p->ut_type == DEAD_PROCESS);
	t_setutxent();
	utx(&e, BOOT_TIME, "", "", "", 0, 0);
	p = t_getutxid(&e);
	check("getutxid: a time type by type", p != 0 && p->ut_type == BOOT_TIME);
	t_setutxent();
	utx(&e, EMPTY, "p1", "", "", 0, 0);
	check("getutxid: EMPTY finds nothing", t_getutxid(&e) == 0);
	t_setutxent();
	utx(&e, 12, "p1", "", "", 0, 0);
	check("getutxid: a type past DEAD_PROCESS finds nothing", t_getutxid(&e) == 0);
	t_setutxent();
	for (n = 0; t_getutxent() != 0; n++)
		;
	check("getutxent: three entries", n == 3);
	t_endutxent();

	/* utmp has entries, utmpx none: utmpx is made from utmp. */
	unlink("t.utmpx");
	fd = open("t.utmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	memset(&ue, 0, sizeof ue);
	memcpy(ue.ut_user, "olduser1", 8);
	memcpy(ue.ut_id, "o1", 2);
	strcpy(ue.ut_line, "console");
	ue.ut_pid = -2;
	ue.ut_type = USER_PROCESS;
	ue.ut_time = 800000000;
	write(fd, &ue, sizeof ue);
	memcpy(ue.ut_id, "o2", 2);
	ue.ut_type = RUN_LVL;
	write(fd, &ue, sizeof ue);
	close(fd);
	close(open("t.utmpx", O_WRONLY | O_CREAT | O_TRUNC, 0644));
	p = t_getutxent();
	check("getutxent: an empty utmpx is made from utmp", p != 0 && fsize("t.utmpx") == 2 * 372);
	memset(&e, 0, sizeof e);
	memcpy(e.ut_user, "olduser1", 8);
	memcpy(e.ut_id, "o1", 2);
	strcpy(e.ut_line, "console");
	e.ut_pid = -2;
	e.ut_type = USER_PROCESS;
	e.ut_tv.tv_sec = 800000000;
	check("utmpx: the converted record, the rest zero",
	      readrec("t.utmpx", 0, &x, sizeof x) && memcmp(&x, &e, sizeof e) == 0);
	t_endutxent();

	/* Both have entries and utmpx is 100 s newer: utmp is made from it. */
	fd = open("t.utmpx", O_WRONLY | O_APPEND);
	utx(&e, USER_PROCESS, "x9", "ttyx9", "newer", 99, 800000050);
	write(fd, &e, sizeof e);
	close(fd);
	settime("t.utmp", 800000000);
	settime("t.utmpx", 800000100);
	t_getutxent();
	check("getutxent: utmp is made from a utmpx 100 s newer",
	      fsize("t.utmp") == 3 * 36 && readrec("t.utmp", 2, &u, sizeof u) && u.ut_pid == 99
	      && u.ut_time == 800000050 && memcmp(u.ut_user, "newer\0\0\0", 8) == 0);
	t_endutxent();

	/* 9 s apart: nothing is converted. */
	fd = open("t.utmp", O_WRONLY | O_APPEND);
	write(fd, &ue, sizeof ue);
	close(fd);
	settime("t.utmp", 800000000);
	settime("t.utmpx", 800000009);
	t_getutxent();
	check("getutxent: files 9 s apart are left alone", fsize("t.utmp") == 4 * 36 && fsize("t.utmpx") == 3 * 372);
	t_endutxent();

	fd = open("t.utmp", O_WRONLY | O_APPEND);
	write(fd, "0123456789", 10);
	close(fd);
	errno = 0;
	check("getutxent: a utmp of part records fails with EINVAL", t_getutxent() == 0 && errno == EINVAL);
	t_endutxent();
	unlink("t.utmpx");
	unlink("t.utmp");
}

int main(void)
{
	test_mnttab();
	test_syslog();
	test_utmpx();
	printf("amixrt-libc-host: %s\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
