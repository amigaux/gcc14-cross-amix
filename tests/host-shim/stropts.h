/* stropts.h for the host test: putmsg's buffers.  The test defines putmsg. */
#ifndef AMIXRT_HOST_STROPTS_H
#define AMIXRT_HOST_STROPTS_H
struct strbuf {
	int maxlen;
	int len;
	char *buf;
};
int putmsg(int, const struct strbuf *, const struct strbuf *, int);
#endif
