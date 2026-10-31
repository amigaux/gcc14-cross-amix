/* solib-main-indirect.c -- a program that links one library, which needs
 * another.
 *
 * Linked by tools/test-shared-indirect.sh as
 * `gcc -o solib-main-indirect solib-main-indirect.c -lsolibmid`, without
 * naming libsolibdep.  Exit status 0 means the clock call made through both
 * libraries returned a plausible date.  The order line is reported, not
 * judged: it shows whether the dependency's constructor ran first.
 */
#include <stdio.h>

int solib_mid_clock(char *, int);
int solib_mid_dep_first(void);

int main(void)
{
	char buf[32];
	int ok = solib_mid_clock(buf, sizeof buf);

	printf("main: constructor order: %s\n",
	       solib_mid_dep_first() == 1 ? "solib-dep before solib-mid"
					  : "solib-mid before solib-dep");
	printf("main: clock through two libraries=%s -> %s\n", buf,
	       ok ? "PASS" : "FAIL");
	fflush(stdout);
	return ok ? 0 : 1;
}
