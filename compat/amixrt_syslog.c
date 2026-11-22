/* amixrt_syslog.c -- openlog, syslog, vsyslog, closelog and setlogmask, for
 * libamixrt.so.
 *
 * libc.so.1 exports none of them; AMIX has them only as one static member of
 * usr/ccs/lib/libc.so, syslog.o, which keeps their state between calls.
 * ToolTalk and CDE's calendar library log through them.  They are one group
 * here too, in one file, so that a program linking libc's member gets all
 * five from the program and a library none from here.  Written from
 * <syslog.h>, <sys/strlog.h> and the log(7) interface, behaving as libc's
 * member does (read off its code):
 *
 *  - Nothing is logged before openlog: syslog without it returns at once.
 *    openlog allocates the state on its first call -- descriptor -1, no
 *    options, ident "syslog", every priority in the mask, facility LOG_USER --
 *    then stores the ident when one is given, the options always, and the
 *    facility when one is given.  With LOG_NDELAY it opens /dev/conslog for
 *    writing, close-on-exec.
 *  - syslog drops a priority of 0 or below, a facility past LOG_LOCAL7 and a
 *    priority outside the mask.  It opens the device with LOG_NDELAY added to
 *    the options when it is not open yet, and takes the openlog facility when
 *    the priority names none.
 *  - The message is "Mmm dd hh:mm:ss " from ctime, the ident, "[pid]" with
 *    LOG_PID, ": " after an ident, then the format, which is cut at its
 *    first newline, has %m replaced by strerror of the errno syslog was
 *    called with ("error N" for an unknown one), gets a newline, and is
 *    formatted into a 1032-byte buffer.  It goes to the log driver with putmsg: a
 *    struct log_ctl with the priority and SL_CONSOLE, and the text with its
 *    null, 1024 bytes at most.
 *  - When putmsg fails and LOG_CONS is set, a vforked child writes the
 *    message -- the text after the time, ident and pid -- with "\r" after
 *    its newline to /dev/syscon, giving up after 5 seconds of SIGALRM; the
 *    parent waits for it unless LOG_NOWAIT.
 *  - closelog closes the device and keeps the state; setlogmask returns the
 *    old mask and sets a non-zero new one.
 *
 * Three differences, all where libc's member goes outside its own data.
 * libc's setlogmask reads and writes through the state without checking
 * that openlog made it, so before openlog it follows a null pointer; this
 * one returns 0 and changes nothing then.  libc's console child writes the
 * whole text's length plus one starting at the message, so after the "\r"
 * it sends a null and as many bytes of stale stack as the header was long;
 * this one stops after the "\r".  libc's formats the text with vsprintf,
 * which runs past the buffer, over the stack, when the message is longer;
 * this one uses vsnprintf (libamixc99's, or the program's) and cuts it.
 * Only the first 1024 bytes are sent either way.
 *
 * vsyslog's buffers are on the stack as in libc's, not in the library's
 * data, which every process would pay for.
 */
#include <sys/types.h>
#include <sys/strlog.h>
#include <syslog.h>
#include <sys/wait.h>
#include <stropts.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* AMIX's <syslog.h> has the constants and declares none of the functions. */
void openlog(const char *, int, int);
void syslog(int, const char *, ...);
void vsyslog(int, const char *, va_list);
void closelog(void);
int setlogmask(int);
extern pid_t vfork(void);
/* Nor does <stdio.h> declare vsnprintf; libamixrt has it from libamixc99. */
int vsnprintf(char *, size_t, const char *, va_list);

_Static_assert(sizeof(struct log_ctl) == 24, "struct log_ctl");
_Static_assert(offsetof(struct log_ctl, flags) == 6, "log_ctl flags");
_Static_assert(offsetof(struct log_ctl, pri) == 20, "log_ctl pri");

#define AMIXRT_LOG_TEXT	1024	/* the text putmsg sends, its null included */

struct amixrt_log {
	int fd;
	int options;
	const char *ident;
	int mask;
	int facility;
};

static struct amixrt_log *amixrt_log;

void openlog(const char *ident, int options, int facility)
{
	struct amixrt_log *l = amixrt_log;

	if (l == 0) {
		l = calloc(1, sizeof *l);
		if (l == 0) return;
		l->fd = -1;
		l->options = 0;
		l->ident = "syslog";
		l->mask = 0xff;
		l->facility = LOG_USER;
		amixrt_log = l;
	}
	if (ident) l->ident = ident;
	l->options = options;
	if (facility) l->facility = facility & LOG_FACMASK;
	if (l->fd < 0 && (l->options & LOG_NDELAY)) {
		l->fd = open("/dev/conslog", O_WRONLY);
		fcntl(l->fd, F_SETFD, 1);
	}
}

/* The console fallback, run in the vforked child.  It ends in _exit, never
 * returning into the parent's frame. */
static void amixrt_log_console(char *msg)
{
	sigset_t set;
	int fd;

	signal(SIGALRM, SIG_DFL);
	sigprocmask(SIG_BLOCK, 0, &set);
	sigdelset(&set, SIGALRM);
	sigprocmask(SIG_SETMASK, &set, 0);
	alarm(5);
	fd = open("/dev/syscon", O_WRONLY);
	if (fd >= 0) {
		alarm(0);
		strcat(msg, "\r");
		write(fd, msg, strlen(msg));
		close(fd);
	}
	_exit(0);
}

void vsyslog(int pri, const char *fmt, va_list ap)
{
	int saved = errno;
	struct amixrt_log *l = amixrt_log;
	char text[1032], format[1024];
	char *p, *f, *end;
	struct log_ctl ctl;
	struct strbuf cbuf, dbuf;
	time_t now;
	int c, len;
	pid_t pid;

	if (l == 0 || pri <= 0) return;
	if (((pri & LOG_FACMASK) >> 3) > LOG_NFACILITIES - 1) return;
	if (!(l->mask & LOG_MASK(pri & LOG_PRIMASK))) return;
	if (l->fd < 0) openlog(l->ident, l->options | LOG_NDELAY, 0);
	if ((pri & LOG_FACMASK) == 0) pri |= l->facility;

	memset(&ctl, 0, sizeof ctl);
	ctl.pri = pri;
	ctl.flags = SL_CONSOLE;
	ctl.level = 0;

	time(&now);
	p = text;
	sprintf(p, "%.15s ", ctime(&now) + 4);
	p += strlen(p);
	if (l->ident) {
		strcpy(p, l->ident);
		p += strlen(p);
	}
	if (l->options & LOG_PID) {
		sprintf(p, "[%d]", (int)getpid());
		p += strlen(p);
	}
	if (l->ident) {
		strcpy(p, ": ");
		p += 2;
	}

	/* The format, up to its first newline, %m replaced; libc leaves 8
	 * bytes of the buffer for the last item's expansion to run into. */
	f = format;
	end = format + sizeof format - 8;
	while ((c = (unsigned char)*fmt++) != 0 && c != '\n' && f < end) {
		if (c != '%') {
			*f++ = (char)c;
			continue;
		}
		c = (unsigned char)*fmt++;
		if (c != 'm') {
			*f++ = '%';
			*f++ = (char)c;
			if (c == 0) break;
			continue;
		}
		{
			const char *e = strerror(saved);

			if (e) strcpy(f, e);
			else sprintf(f, "error %d", saved);
			f += strlen(f);
		}
	}
	*f++ = '\n';
	*f = 0;
	vsnprintf(p, sizeof text - (size_t)(p - text), format, ap);

	len = (int)strlen(text) + 1;
	if (len > AMIXRT_LOG_TEXT) len = AMIXRT_LOG_TEXT;
	cbuf.maxlen = sizeof ctl;
	cbuf.len = sizeof ctl;
	cbuf.buf = (char *)&ctl;
	dbuf.maxlen = AMIXRT_LOG_TEXT + 1;
	dbuf.len = len;
	dbuf.buf = text;
	if (putmsg(l->fd, &cbuf, &dbuf, 0) >= 0) return;
	if (!(l->options & LOG_CONS)) return;
	pid = vfork();
	if (pid == -1) return;
	if (pid == 0) amixrt_log_console(p);
	if (!(l->options & LOG_NOWAIT)) waitpid(pid, 0, 0);
}

void syslog(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(pri, fmt, ap);
	va_end(ap);
}

void closelog(void)
{
	if (amixrt_log == 0) return;
	close(amixrt_log->fd);
	amixrt_log->fd = -1;
}

int setlogmask(int mask)
{
	int old;

	if (amixrt_log == 0) return 0;
	old = amixrt_log->mask;
	if (mask) amixrt_log->mask = mask;
	return old;
}
