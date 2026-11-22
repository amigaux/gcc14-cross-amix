/* utmp.h for the host test: AMIX's 36-byte utmp record. */
#ifndef AMIXRT_HOST_UTMP_H
#define AMIXRT_HOST_UTMP_H
#include <stdint.h>
#define UTMP_FILE	"/var/adm/utmp"
struct exit_status {
	int16_t e_termination;
	int16_t e_exit;
};
struct utmp {
	char ut_user[8];
	char ut_id[4];
	char ut_line[12];
	int16_t ut_pid;
	int16_t ut_type;
	struct exit_status ut_exit;
	int32_t ut_time;
};
#define EMPTY		0
#define RUN_LVL		1
#define BOOT_TIME	2
#define OLD_TIME	3
#define NEW_TIME	4
#define INIT_PROCESS	5
#define LOGIN_PROCESS	6
#define USER_PROCESS	7
#define DEAD_PROCESS	8
#define ACCOUNTING	9
#endif
