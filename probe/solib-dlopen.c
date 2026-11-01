/* solib-dlopen.c -- load a library stack with dlopen.
 *
 * Linked by tools/test-amixrt.sh with no shared library of ours, only -ldl.
 * It opens libsolibmid.so.1, whose NEEDED brings in libsolibdep.so.1, whose
 * NEEDED brings in libamixrt.so.1 for gettimeofday and snprintf: none of those
 * three is in the program, so the runtime linker has to load them all.  The
 * specs turn the call into __wrap_dlopen (compat/amix_dlopen.c), which has
 * the program's runner queue the libraries' constructors while _dlopen runs
 * and run them afterwards, dependencies first.  Exit status 0 means the call
 * through both libraries returned a plausible date and libsolibdep's
 * constructor ran before libsolibmid's.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
	char buf[32];
	void *h;
	int (*clock_fn)(char *, int);
	int (*dep_first)(void);
	int ok;

	printf("main: before dlopen\n");
	fflush(stdout);
	h = dlopen("libsolibmid.so.1", RTLD_NOW);
	if (h == 0) {
		printf("main: dlopen failed: %s -> FAIL\n", dlerror());
		return 1;
	}
	clock_fn = (int (*)(char *, int))dlsym(h, "solib_mid_clock");
	dep_first = (int (*)(void))dlsym(h, "solib_mid_dep_first");
	if (clock_fn == 0 || dep_first == 0) {
		printf("main: dlsym failed: %s -> FAIL\n", dlerror());
		return 1;
	}
	ok = clock_fn(buf, sizeof buf);
	{
		time_t s = (time_t)atol(buf);

		printf("main: the machine's clock reads %s      (the clock check needs a date after 2020-01-01)\n", ctime(&s));
	}
	printf("main: constructor order: %s\n",
	       dep_first() == 1 ? "solib-dep before solib-mid" : "solib-mid before solib-dep");
	printf("main: clock through two dlopen'd libraries=%s (%s)\n", buf,
	       ok ? "plausible" : "WRONG");
	ok = ok && dep_first() == 1;
	printf("main: %s\n", ok ? "PASS" : "FAIL");
	fflush(stdout);
	dlclose(h);
	return ok ? 0 : 1;
}
