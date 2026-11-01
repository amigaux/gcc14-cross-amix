/* solib-main-indirect.c -- a program that links one library, which needs
 * another.
 *
 * Linked by tools/test-shared-indirect.sh as
 * `gcc -o solib-main-indirect solib-main-indirect.c -lsolibmid`, without
 * naming libsolibdep.  Exit status 0 means the clock call made through both
 * libraries, and libamixrt's gettimeofday, returned a plausible date, and
 * libsolibdep's constructor ran before libsolibmid's: the runtime linker
 * calls DT_INIT in load order, the library before what it NEEDs, and the
 * program's start-up runs the deferred constructors the other way round.
 */
#include <stdio.h>

int solib_mid_clock(char *, int);
int solib_mid_dep_first(void);

int main(void)
{
	char buf[32];
	int clock_ok = solib_mid_clock(buf, sizeof buf);
	int dep_first = solib_mid_dep_first() == 1;
	int ok = clock_ok && dep_first;

	printf("main: constructor order: %s\n",
	       dep_first ? "solib-dep before solib-mid" : "solib-mid before solib-dep");
	printf("main: clock through two libraries=%s (%s) -> %s\n", buf,
	       clock_ok ? "plausible" : "WRONG", ok ? "PASS" : "FAIL");
	fflush(stdout);
	return ok ? 0 : 1;
}
