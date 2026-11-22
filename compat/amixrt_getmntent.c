/* amixrt_getmntent.c -- getmntent and getmntany, for libamixrt.so.
 *
 * libc.so.1 exports neither; AMIX has them only as a static member of
 * usr/ccs/lib/libc.so, getmntent.o.  ToolTalk reads /etc/mnttab with
 * getmntent.  Written from <sys/mnttab.h> and the mnttab(4) format, and
 * behaving as libc's member does (read off its code):
 *
 *  - getmntent reads the next line that is not blank and not a comment
 *    (first character after leading blanks and tabs '\n' or '#') with fgets
 *    into a 1024-byte buffer of its own, and returns -1 at the end of the
 *    file.  A line of 1023 characters with no newline returns MNT_TOOLONG,
 *    and the rest of it is read as the next line.
 *  - The fields are split with strtok on blank, tab and newline: special,
 *    mount point, file system type, options, time.  A field of "-" reads as
 *    a null pointer.  Fewer than five give MNT_TOOFEW, more give MNT_TOOMANY
 *    (the five are filled in either way), else 0.  The pointers point into
 *    the buffer, which the next call overwrites.
 *  - getmntany returns the next entry matching every non-null field of the
 *    reference: the strings compare equal, except that a reference special
 *    naming a block or character device matches an entry whose special
 *    stats as the same kind of device with the same st_rdev.  It returns
 *    getmntent's result for the matching entry, or the first non-zero one.
 *
 * The buffer is allocated on the first call rather than kept in the
 * library's data, so a process that never reads mnttab does not pay for it.
 * strtok is libc's, as with libc's member: its position is shared with the
 * caller's.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>		/* before <sys/mnttab.h>, which uses FILE */
#include <sys/mnttab.h>
#include <stdlib.h>
#include <string.h>

#define AMIXRT_SEP	" \t\n"

static char *amixrt_mntline;

/* 0 for a line, -1 at the end of the file, MNT_TOOLONG. */
static int amixrt_mntread(char *line, FILE *fp)
{
	char *p;

	while (fgets(line, MNT_LINE_MAX, fp) != 0) {
		if (strlen(line) == MNT_LINE_MAX - 1 && line[MNT_LINE_MAX - 2] != '\n')
			return MNT_TOOLONG;
		for (p = line; *p == ' ' || *p == '\t'; p++)
			;
		if (*p != '#' && *p != '\n') return 0;
	}
	return -1;
}

int getmntent(FILE *fp, struct mnttab *mp)
{
	char **field[5];
	int i, r;

	if (amixrt_mntline == 0 && (amixrt_mntline = malloc(MNT_LINE_MAX)) == 0)
		return -1;
	r = amixrt_mntread(amixrt_mntline, fp);
	if (r != 0) return r;
	field[0] = &mp->mnt_special;
	field[1] = &mp->mnt_mountp;
	field[2] = &mp->mnt_fstype;
	field[3] = &mp->mnt_mntopts;
	field[4] = &mp->mnt_time;
	for (i = 0; i < 5; i++) {
		*field[i] = strtok(i == 0 ? amixrt_mntline : 0, AMIXRT_SEP);
		if (*field[i] == 0) return MNT_TOOFEW;
		if (strcmp(*field[i], "-") == 0) *field[i] = 0;
	}
	return strtok(0, AMIXRT_SEP) != 0 ? MNT_TOOMANY : 0;
}

/* 1 if the reference's field is null or names the entry's. */
static int amixrt_mntsame(const char *ref, const char *ent)
{
	return ref == 0 || (ent != 0 && strcmp(ref, ent) == 0);
}

int getmntany(FILE *fp, struct mnttab *mp, struct mnttab *mpref)
{
	struct stat st;
	int bydev = 0, r;
	mode_t fmt = 0;
	dev_t rdev = 0;

	if (mpref->mnt_special != 0 && stat(mpref->mnt_special, &st) == 0
	    && ((st.st_mode & S_IFMT) == S_IFBLK || (st.st_mode & S_IFMT) == S_IFCHR)) {
		bydev = 1;
		fmt = st.st_mode & S_IFMT;
		rdev = st.st_rdev;
	}
	while ((r = getmntent(fp, mp)) == 0) {
		if (bydev) {
			if (mp->mnt_special == 0 || stat(mp->mnt_special, &st) == -1
			    || (st.st_mode & S_IFMT) != fmt || st.st_rdev != rdev)
				continue;
		} else if (!amixrt_mntsame(mpref->mnt_special, mp->mnt_special)) {
			continue;
		}
		if (amixrt_mntsame(mpref->mnt_mountp, mp->mnt_mountp)
		    && amixrt_mntsame(mpref->mnt_fstype, mp->mnt_fstype)
		    && amixrt_mntsame(mpref->mnt_mntopts, mp->mnt_mntopts)
		    && amixrt_mntsame(mpref->mnt_time, mp->mnt_time))
			return 0;
	}
	return r;
}
