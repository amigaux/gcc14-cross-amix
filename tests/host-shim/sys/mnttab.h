/* sys/mnttab.h for the host test: struct mnttab and getmntent's results. */
#ifndef AMIXRT_HOST_MNTTAB_H
#define AMIXRT_HOST_MNTTAB_H
#include <stdio.h>
#define MNTTAB		"/etc/mnttab"
#define MNT_LINE_MAX	1024
#define MNT_TOOLONG	1
#define MNT_TOOMANY	2
#define MNT_TOOFEW	3
struct mnttab {
	char *mnt_special;
	char *mnt_mountp;
	char *mnt_fstype;
	char *mnt_mntopts;
	char *mnt_time;
};
#endif
