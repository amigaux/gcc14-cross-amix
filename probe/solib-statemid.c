/* solib-statemid.c -- the library a program links to reach libsolibstate.
 *
 * Built by tools/test-amixrt.sh as libsolibstatemid.so.1, linked against
 * libsolibstate.so.1, and only forwards, so that libsolibstate is reached
 * through its NEEDED.
 */
long st_lrand48(void);
void st_srand48(long);
int st_logmask(void);
int st_setlogmask(int);
long st_itimer_interval(void);
int st_setitimer(long, long);

long mid_lrand48(void) { return st_lrand48(); }
void mid_srand48(long s) { st_srand48(s); }
int mid_logmask(void) { return st_logmask(); }
int mid_setlogmask(int m) { return st_setlogmask(m); }
long mid_itimer_interval(void) { return st_itimer_interval(); }
int mid_setitimer(long i, long v) { return st_setitimer(i, v); }
