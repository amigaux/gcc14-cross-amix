/* utmpx.h for the host test: AMIX's 372-byte utmpx record. */
#ifndef AMIXRT_HOST_UTMPX_H
#define AMIXRT_HOST_UTMPX_H
#include <utmp.h>
#define UTMPX_FILE	"/var/adm/utmpx"
struct amixrt_host_timeval {
	int32_t tv_sec;
	int32_t tv_usec;
};
struct utmpx {
	char ut_user[32];
	char ut_id[4];
	char ut_line[32];
	int32_t ut_pid;
	int16_t ut_type;
	struct exit_status ut_exit;
	struct amixrt_host_timeval ut_tv;
	int32_t ut_session;
	int32_t pad[5];
	int16_t ut_syslen;
	char ut_host[257];
};
void endutxent(void);
struct utmpx *getutxent(void);
struct utmpx *getutxid(const struct utmpx *);
struct utmpx *getutxline(const struct utmpx *);
struct utmpx *pututxline(const struct utmpx *);
void setutxent(void);
int utmpxname(const char *);
#endif
