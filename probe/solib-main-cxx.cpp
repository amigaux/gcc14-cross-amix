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

struct Mine {
	Mine()  { printf("main-cxx: static object constructed\n"); fflush(stdout); }
	~Mine() { printf("main-cxx: static object destroyed\n"); fflush(stdout); }
};

static Mine mine;

int main()
{
	int value = solib_cxx_value();

	printf("main-cxx: value=%d (7) -> %s\n", value, value == 7 ? "PASS" : "FAIL");
	fflush(stdout);
	return value == 7 ? 0 : 1;
}
