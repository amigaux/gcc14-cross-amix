/* solib-cxx.cpp -- a C++ shared library with a static object.
 *
 * Built by tools/test-shared.sh with `gcc -shared -fPIC`: gcc, not g++, so the
 * C++ runtime stays in the executable rather than a second static copy of it
 * going into the library.  The object's constructor is an .init_array entry,
 * run from the library's DT_INIT; its destructor is registered with
 * __cxa_atexit, which the executable supplies from libamixc99.
 */
#include <stdio.h>

struct Registry {
	int value;
	Registry() : value(7)
	{
		printf("solib-cxx: static object constructed\n");
		fflush(stdout);
	}
	~Registry()
	{
		printf("solib-cxx: static object destroyed\n");
		fflush(stdout);
	}
};

static Registry registry;

extern "C" int solib_cxx_value(void)
{
	return registry.value;
}
