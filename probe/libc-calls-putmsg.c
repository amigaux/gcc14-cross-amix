/* libc-calls-putmsg.c -- a putmsg that prints what syslog sends.
 *
 * Linked into both programs of probe/libc-calls.c.  libc's syslog member
 * calls _putmsg, libamixrt's calls putmsg; the program defines both, and
 * because libc.so.1 defines them too the program exports them, so the
 * runtime linker sends libamixrt's call here as well.  It prints the
 * control part's level, flags and priority and the text, with the time
 * stamp and a process id in brackets masked so that two runs compare equal,
 * and reports success: nothing reaches the log driver.
 */
#include <sys/types.h>
#include <sys/strlog.h>
#include <stropts.h>
#include <stdio.h>
#include <string.h>

static int lc_putmsg(int fd, const struct strbuf *ctl, const struct strbuf *data, int flags)
{
	const struct log_ctl *lc = (const struct log_ctl *)ctl->buf;
	const char *t = data->buf, *b, *e;
	int pidlen;

	/* "Mmm dd hh:mm:ss " is 16 characters; the pid's digits are not
	 * counted in the length printed, since the two programs' differ. */
	b = strchr(t, '[');
	e = b ? strchr(b, ']') : 0;
	pidlen = b && e ? (int)(e - b - 1) : 0;
	printf("putmsg: fd %s, flags %d, ctl len %d maxlen %d, level %d, flags %#x, pri %d (%d|%d),"
	       " data len %d+pid maxlen %d\n", fd >= 0 ? "open" : "closed", flags, ctl->len,
	       ctl->maxlen, lc->level, lc->flags, lc->pri, lc->pri & 0x3f8, lc->pri & 7,
	       data->len - pidlen, data->maxlen);
	if (strlen(t) > 16) {
		printf("putmsg: <time> ");
		if (b && e) printf("%.*s[<pid>]%s", (int)(b - t - 16), t + 16, e + 1);
		else printf("%s", t + 16);
	}
	if (data->len > 0 && t[data->len - 1] != 0) printf("putmsg: the text has no null\n");
	fflush(stdout);
	return 0;
}

int putmsg(int fd, const struct strbuf *ctl, const struct strbuf *data, int flags)
{
	return lc_putmsg(fd, ctl, data, flags);
}

int _putmsg(int fd, const struct strbuf *ctl, const struct strbuf *data, int flags)
{
	return lc_putmsg(fd, ctl, data, flags);
}
