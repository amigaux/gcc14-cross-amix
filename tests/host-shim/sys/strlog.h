/* sys/strlog.h for the host test: the log driver's 24-byte control part. */
#ifndef AMIXRT_HOST_STRLOG_H
#define AMIXRT_HOST_STRLOG_H
#include <stdint.h>
struct log_ctl {
	int16_t mid;
	int16_t sid;
	char level;
	int16_t flags;
	int32_t ltime;
	int32_t ttime;
	int32_t seq_no;
	int32_t pri;
};
#define SL_CONSOLE	0x10
#endif
