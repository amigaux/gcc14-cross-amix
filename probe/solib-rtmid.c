/* solib-rtmid.c -- the library a program links to reach libsolibrt.
 *
 * Built by tools/test-amixrt.sh as libsolibrtmid.so.1, linked against
 * libsolibrt.so.1.  It only forwards, so that libsolibrt is reached through
 * its NEEDED and the program's link supplies nothing to it.
 */
long rt_now(void);
long rt_select_sleep_us(long);
long rt_usleep_us(long);
int rt_select_pipe(void);
long rt_truncate(const char *);
char *rt_realpath(const char *, char *);
int rt_setitimer(long, long);
long rt_getitimer(void);
int rt_tables(void);
int rt_snprintf(char *, int);

long mid_now(void) { return rt_now(); }
long mid_select_sleep_us(long ms) { return rt_select_sleep_us(ms); }
long mid_usleep_us(long us) { return rt_usleep_us(us); }
int mid_select_pipe(void) { return rt_select_pipe(); }
long mid_truncate(const char *p) { return rt_truncate(p); }
char *mid_realpath(const char *p, char *b) { return rt_realpath(p, b); }
int mid_setitimer(long i, long v) { return rt_setitimer(i, v); }
long mid_getitimer(void) { return rt_getitimer(); }
int mid_tables(void) { return rt_tables(); }
int mid_snprintf(char *b, int n) { return rt_snprintf(b, n); }
