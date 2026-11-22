/* libc-calls-mid.c -- the library libc-amixrt links to reach libsolibsc.
 *
 * Built by tools/test-amixrt.sh as libsolibscmid.so.1, linked against
 * libsolibsc.so.1 (probe/libc-calls.c).  It only forwards, so that libsolibsc
 * and libamixrt.so.1 are reached through NEEDED entries and the program's
 * link supplies none of the functions under test.
 */
int lc_run(const char *);

int mid_lc_run(const char *dir) { return lc_run(dir); }
