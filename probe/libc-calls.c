/* libc-calls.c -- libamixrt's second wave against libc's own members.
 *
 * siglongjmp, vfork, _fpsetround, _fpsetsticky, the syslog group,
 * getmntent and getmntany, and the utmpx group: libc.so.1 exports none of
 * them, so AMIX has them only as static members of usr/ccs/lib/libc.so, and
 * libamixrt.so.1 has its own for shared libraries.  tools/test-amixrt.sh
 * compiles this file twice: into the program libc-static, which links libc's
 * members, and into the library libsolibsc.so.1, which libc-amixrt reaches
 * only through libsolibscmid.so.1, so that its calls are libamixrt's.  Both
 * print what they see in the same words, and run-amixrt.sh compares the two
 * outputs: any difference is a difference from libc.
 *
 * Everything printed is made deterministic: the floating-point registers are
 * loaded with known values before each call, the utmpx files are scratch
 * files with fixed times in /tmp, and syslog's message goes to a putmsg the
 * program defines (probe/libc-calls-putmsg.c), which prints it with the time
 * and the process id masked.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mnttab.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <utmp.h>
#include <utmpx.h>

extern pid_t vfork(void);
/* AMIX's <syslog.h> declares none of these. */
extern void openlog(const char *, int, int);
extern void syslog(int, const char *, ...);
extern void closelog(void);
extern int setlogmask(int);
extern int _fpsetround(int);
extern int _fpsetsticky(int);
extern int __flt_rounds;

static void lc_flush(void)
{
	fflush(stdout);
}

/* siglongjmp back into sigsetjmp: the values it returns, locals kept in
 * registers across it, and whether the signal mask comes back. */
static sigjmp_buf lc_env;
static int lc_jumps;

static void lc_siglongjmp(void)
{
	register int keep1 = 11, keep2 = 22;
	sigset_t set, now;
	volatile int r;

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	lc_jumps = 0;
	r = sigsetjmp(lc_env, 1);
	printf("siglongjmp: sigsetjmp returned %d, locals %d %d\n", r, keep1, keep2);
	if (lc_jumps == 0) {
		lc_jumps = 1;
		sigprocmask(SIG_BLOCK, &set, 0);
		siglongjmp(lc_env, 0);
	}
	if (lc_jumps == 1) {
		lc_jumps = 2;
		siglongjmp(lc_env, 42);
	}
	sigprocmask(SIG_BLOCK, 0, &now);
	printf("siglongjmp: SIGUSR1 blocked after the jumps: %s\n",
	       sigismember(&now, SIGUSR1) ? "yes" : "no");
	sigprocmask(SIG_UNBLOCK, &set, 0);
	lc_flush();
}

/* vfork: the child runs in the parent's memory until it exits. */
static volatile int lc_shared;

static void lc_vfork(void)
{
	int status = -1, local = 5;
	pid_t pid;

	lc_shared = 0;
	lc_flush();
	pid = vfork();
	if (pid == 0) {
		lc_shared = 1;
		_exit(7);
	}
	if (pid < 0) {
		printf("vfork: failed, errno %d\n", errno);
		return;
	}
	waitpid(pid, &status, 0);
	printf("vfork: child exit %d, its store seen by the parent %d, local %d\n",
	       WIFEXITED(status) ? WEXITSTATUS(status) : -1, lc_shared, local);
	lc_flush();
}

static unsigned long lc_fpcr(void)
{
	unsigned long v;

	__asm__ volatile ("fmovel %%fpcr, %0" : "=d" (v));
	return v;
}

static void lc_set_fpcr(unsigned long v)
{
	__asm__ volatile ("fmovel %0, %%fpcr" : : "d" (v));
}

static unsigned long lc_fpsr(void)
{
	unsigned long v;

	__asm__ volatile ("fmovel %%fpsr, %0" : "=d" (v));
	return v;
}

static void lc_set_fpsr(unsigned long v)
{
	__asm__ volatile ("fmovel %0, %%fpsr" : : "d" (v));
}

static void lc_fp(void)
{
	static const unsigned long fpcrs[] = { 0, 0x30, 0x80, 0x00003c50 };
	static const unsigned long fpsrs[] = { 0, 0xf8, 0x0f000a00, 0x0800ff00 };
	unsigned i;
	int r;

	for (i = 0; i < sizeof fpcrs / sizeof fpcrs[0]; i++) {
		int mode;

		for (mode = 0; mode <= 4; mode++) {
			lc_set_fpcr(fpcrs[i]);
			r = _fpsetround(mode);
			printf("_fpsetround(%d) from FPCR %08lx: returned %d, FPCR %08lx, __flt_rounds %d\n",
			       mode, fpcrs[i], r, lc_fpcr(), __flt_rounds);
		}
	}
	lc_set_fpcr(0);
	_fpsetround(0);
	for (i = 0; i < sizeof fpsrs / sizeof fpsrs[0]; i++) {
		static const int sticky[] = { 0, 0x10, 0xf8, 0x1ff };
		unsigned j;

		for (j = 0; j < sizeof sticky / sizeof sticky[0]; j++) {
			lc_set_fpsr(fpsrs[i]);
			r = _fpsetsticky(sticky[j]);
			printf("_fpsetsticky(%#x) from FPSR %08lx: returned %08x, FPSR %08lx\n",
			       sticky[j], fpsrs[i], (unsigned)r, lc_fpsr());
		}
	}
	lc_set_fpsr(0);
	lc_flush();
}

static void lc_syslog(void)
{
	int old;

	openlog("amixrt-probe", LOG_PID, LOG_LOCAL0);
	old = setlogmask(LOG_UPTO(LOG_INFO));
	printf("syslog: setlogmask returned %#x, then %#x\n", old, setlogmask(0));
	lc_flush();
	errno = ENOENT;
	syslog(LOG_INFO, "probe %s %d: %m, 100%%\nnot sent", "message", 7);
	syslog(LOG_DEBUG, "masked out");
	syslog(LOG_ERR | LOG_MAIL, "facility %s", "mail");
	syslog(0, "priority 0 is dropped");
	syslog(LOG_ERR | (24 << 3), "facility 24 is dropped");
	closelog();
	syslog(LOG_NOTICE, "reopened %s", "after closelog");
	openlog(0, 0, 0);
	syslog(LOG_WARNING, "options cleared, ident %s", "kept");
	closelog();
	lc_flush();
}

static void lc_mntprint(const char *what, int r, struct mnttab *m)
{
	printf("%s: %d", what, r);
	if (r == 0 || r == 2 || r == 3)
		printf(" [%s] [%s] [%s] [%s] [%s]", m->mnt_special ? m->mnt_special : "(null)",
		       m->mnt_mountp ? m->mnt_mountp : "(null)",
		       m->mnt_fstype ? m->mnt_fstype : "(null)",
		       m->mnt_mntopts ? m->mnt_mntopts : "(null)",
		       m->mnt_time ? m->mnt_time : "(null)");
	printf("\n");
}

static void lc_mnttab(const char *dir)
{
	char path[256], longline[1100];
	struct mnttab m, ref;
	FILE *fp;
	int r, n;

	sprintf(path, "%s/mnttab.%ld", dir, (long)getpid());
	fp = fopen(path, "w");
	if (fp == 0) {
		printf("getmntent: cannot write %s\n", path);
		return;
	}
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
	fclose(fp);

	fp = fopen(path, "r");
	for (n = 0; n < 12 && (r = getmntent(fp, &m)) != -1; n++)
		lc_mntprint("getmntent", r, &m);
	printf("getmntent: then %d\n", getmntent(fp, &m));
	fclose(fp);

	memset(&ref, 0, sizeof ref);
	ref.mnt_mountp = "/usr";
	fp = fopen(path, "r");
	lc_mntprint("getmntany mountp /usr", getmntany(fp, &m, &ref), &m);
	fclose(fp);
	memset(&ref, 0, sizeof ref);
	ref.mnt_fstype = "proc";
	fp = fopen(path, "r");
	lc_mntprint("getmntany fstype proc", getmntany(fp, &m, &ref), &m);
	fclose(fp);
	unlink(path);

	fp = fopen(MNTTAB, "r");
	if (fp == 0) {
		printf("getmntent: no %s\n", MNTTAB);
	} else {
		for (n = 0; n < 32 && (r = getmntent(fp, &m)) != -1; n++)
			lc_mntprint("mnttab", r, &m);
		fclose(fp);
		memset(&ref, 0, sizeof ref);
		ref.mnt_mountp = "/";
		fp = fopen(MNTTAB, "r");
		lc_mntprint("mnttab getmntany mountp /", getmntany(fp, &m, &ref), &m);
		fclose(fp);
	}
	lc_flush();
}

/* The records of a file, as the fields that matter and a sum of all bytes. */
static void lc_dumpfile(const char *what, const char *path, int x)
{
	unsigned char rec[372];
	unsigned long sum = 0;
	int fd, n, size = x ? 372 : 36, i, count = 0;
	struct stat st;

	if (stat(path, &st) != 0) {
		printf("  %s: missing\n", what);
		return;
	}
	printf("  %s: %ld bytes, mode %03o\n", what, (long)st.st_size, (unsigned)st.st_mode & 0777);
	fd = open(path, O_RDONLY);
	while ((n = read(fd, rec, size)) == size) {
		for (i = 0; i < size; i++) sum = sum * 31 + rec[i];
		if (x) {
			struct utmpx *u = (struct utmpx *)rec;

			printf("    [%d] type %d id %.4s line %.32s user %.32s pid %ld exit %d/%d tv %ld.%06ld"
			       " session %ld syslen %d host %.20s\n", count, u->ut_type, u->ut_id,
			       u->ut_line, u->ut_user, (long)u->ut_pid, u->ut_exit.e_termination,
			       u->ut_exit.e_exit, (long)u->ut_tv.tv_sec, (long)u->ut_tv.tv_usec,
			       u->ut_session, u->ut_syslen, u->ut_host);
		} else {
			struct utmp *u = (struct utmp *)rec;

			printf("    [%d] type %d id %.4s line %.12s user %.8s pid %d exit %d/%d time %ld\n",
			       count, u->ut_type, u->ut_id, u->ut_line, u->ut_user, u->ut_pid,
			       u->ut_exit.e_termination, u->ut_exit.e_exit, (long)u->ut_time);
		}
		count++;
	}
	if (n > 0) printf("    %d bytes left over\n", n);
	close(fd);
	printf("    byte sum %08lx\n", sum);
}

static void lc_utx(struct utmpx *u, short type, const char *id, const char *line,
		   const char *user, long pid, long t)
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

static void lc_utshow(const char *what, struct utmpx *u)
{
	if (u == 0) printf("%s: NULL\n", what);
	else printf("%s: type %d id %.4s line %.32s user %.32s pid %ld\n", what, u->ut_type,
		    u->ut_id, u->ut_line, u->ut_user, (long)u->ut_pid);
}

static void lc_settime(const char *path, long t)
{
	struct utimbuf tb;

	tb.actime = t;
	tb.modtime = t;
	utime(path, &tb);
}

static void lc_utmpx(const char *dir)
{
	char ux[256], u[256];
	struct utmpx e, *p;
	struct utmp ue;
	int fd, n;

	sprintf(ux, "%s/amixrt-lc.%ld.utmpx", dir, (long)getpid());
	strcpy(u, ux);
	u[strlen(u) - 1] = 0;
	unlink(ux);
	unlink(u);
	printf("utmpxname: a name without x %d, too long %d, %s %d\n",
	       utmpxname("/tmp/amixrt.utmp"), utmpxname("/tmp/0123456789012345678901234567890123456789"
					       "0123456789012345678901234567890123456789x"),
	       "a scratch file", utmpxname(ux));

	p = getutxent();
	lc_utshow("getutxent on new files", p);
	lc_dumpfile("utmpx", ux, 1);
	lc_dumpfile("utmp", u, 0);

	lc_utx(&e, USER_PROCESS, "p1", "ttyp1", "probe", 1234, 900000000);
	lc_utshow("pututxline p1", pututxline(&e));
	lc_utx(&e, LOGIN_PROCESS, "p2", "ttyp2", "LOGIN", 1235, 900000010);
	lc_utshow("pututxline p2", pututxline(&e));
	lc_utx(&e, BOOT_TIME, "", "system boot", "", 0, 899999000);
	lc_utshow("pututxline boot", pututxline(&e));
	lc_utx(&e, DEAD_PROCESS, "p1", "ttyp1", "probe", 1234, 900000020);
	e.ut_exit.e_termination = 1;
	e.ut_exit.e_exit = 2;
	lc_utshow("pututxline p1 dead", pututxline(&e));
	lc_dumpfile("utmpx", ux, 1);
	lc_dumpfile("utmp", u, 0);

	setutxent();
	lc_utx(&e, USER_PROCESS, "", "ttyp2", "", 0, 0);
	lc_utshow("getutxline ttyp2", getutxline(&e));
	lc_utshow("getutxline ttyp2 again, from there", getutxline(&e));
	setutxent();
	lc_utx(&e, USER_PROCESS, "", "ttyp1", "", 0, 0);
	lc_utshow("getutxline ttyp1 (dead)", getutxline(&e));
	setutxent();
	lc_utx(&e, INIT_PROCESS, "p1", "", "", 0, 0);
	lc_utshow("getutxid p1", getutxid(&e));
	setutxent();
	lc_utx(&e, BOOT_TIME, "", "", "", 0, 0);
	lc_utshow("getutxid boot", getutxid(&e));
	setutxent();
	lc_utx(&e, EMPTY, "p1", "", "", 0, 0);
	lc_utshow("getutxid empty", getutxid(&e));
	setutxent();
	lc_utx(&e, 12, "p1", "", "", 0, 0);
	lc_utshow("getutxid type 12", getutxid(&e));
	setutxent();
	n = 0;
	while (getutxent() != 0) n++;
	printf("getutxent: %d entries\n", n);
	endutxent();

	/* utmp has entries and utmpx is empty: utmpx is made from utmp. */
	unlink(ux);
	unlink(u);
	fd = open(u, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	memset(&ue, 0, sizeof ue);
	memcpy(ue.ut_user, "olduser1", sizeof ue.ut_user);
	strncpy(ue.ut_id, "o1", sizeof ue.ut_id);
	strncpy(ue.ut_line, "console", sizeof ue.ut_line);
	ue.ut_pid = -2;
	ue.ut_type = USER_PROCESS;
	ue.ut_time = 800000000;
	write(fd, &ue, sizeof ue);
	strncpy(ue.ut_id, "o2", sizeof ue.ut_id);
	ue.ut_type = RUN_LVL;
	write(fd, &ue, sizeof ue);
	close(fd);
	close(open(ux, O_WRONLY | O_CREAT | O_TRUNC, 0644));
	lc_utshow("getutxent, utmpx made from utmp", getutxent());
	lc_dumpfile("utmpx", ux, 1);
	endutxent();

	/* Both have entries; utmpx is 100 s newer: utmp is made from utmpx. */
	lc_settime(u, 800000000);
	lc_settime(ux, 800000100);
	fd = open(ux, O_WRONLY | O_APPEND);
	lc_utx(&e, USER_PROCESS, "x9", "ttyx9", "newer", 99, 800000050);
	write(fd, &e, sizeof e);
	close(fd);
	lc_settime(ux, 800000100);
	lc_utshow("getutxent, utmp made from the newer utmpx", getutxent());
	lc_dumpfile("utmp", u, 0);
	endutxent();

	/* Times within 9 s: nothing is converted. */
	fd = open(u, O_WRONLY | O_APPEND);
	write(fd, &ue, sizeof ue);
	close(fd);
	lc_settime(u, 800000000);
	lc_settime(ux, 800000009);
	lc_utshow("getutxent, times 9 s apart", getutxent());
	lc_dumpfile("utmp", u, 0);
	endutxent();

	/* A utmp that is not whole records. */
	fd = open(u, O_WRONLY | O_APPEND);
	write(fd, "0123456789", 10);
	close(fd);
	errno = 0;
	p = getutxent();
	printf("getutxent with a broken utmp: %s, errno %d\n", p ? "an entry" : "NULL", errno);
	endutxent();

	unlink(ux);
	unlink(u);
	utmpxname(UTMPX_FILE);
	lc_flush();
}

int lc_run(const char *dir)
{
	printf("== siglongjmp\n");
	lc_siglongjmp();
	printf("== vfork\n");
	lc_vfork();
	printf("== _fpsetround, _fpsetsticky\n");
	lc_fp();
	printf("== syslog\n");
	lc_syslog();
	printf("== getmntent, getmntany\n");
	lc_mnttab(dir);
	printf("== the utmpx group\n");
	lc_utmpx(dir);
	printf("== done\n");
	lc_flush();
	return 0;
}
