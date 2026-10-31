/* solib-main-cxx.cpp -- a C++ program with its own static object, linked
 * against the C++ test library.
 *
 * Both objects register their destructors with __cxa_atexit, into the one
 * list in the executable.  At exit libamixc99's hook runs both, the program's
 * first, before stdio's _cleanup; the library's DT_FINI then calls
 * __cxa_finalize for its own entries and finds none left.
 */
#include <stdio.h>

extern "C" int solib_cxx_value(void);
extern "C" int solib_cxx_env(void);

struct Mine {
	Mine()  { printf("main-cxx: static object constructed\n"); fflush(stdout); }
	~Mine() { printf("main-cxx: static object destroyed\n"); fflush(stdout); }
};

static Mine mine;

int main()
{
	int value = solib_cxx_value(), env = solib_cxx_env();
	int ok = value == 7 && env == 1;

	printf("main-cxx: value=%d (7) env=%d (1) -> %s\n", value, env, ok ? "PASS" : "FAIL");
	fflush(stdout);
	return ok ? 0 : 1;
}
