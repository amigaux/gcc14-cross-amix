/* solib-c.c -- a C shared library with a constructor and a destructor.
 *
 * Built by tools/test-shared.sh with `gcc -shared -fPIC`.  AMIX's runtime
 * linker runs a shared object's DT_INIT when it loads it and its DT_FINI at
 * exit, and knows nothing of .init_array -- so the constructor below runs only
 * if amix_initS.o is in the link and walks the array from _init.
 *
 * Each function also exercises one thing a shared library has to get right:
 * initialised pointers (R_68K_RELATIVE), a 64-bit divide from libgcc (linked
 * into the object, hidden), and snprintf, which SVR4.0's libc does not have:
 * the library leaves it undefined and the executable supplies it from
 * libamixc99.
 */
#include <stdio.h>

static int ready;
static const char *words[] = { "zero", "one", "two" };

__attribute__((constructor)) static void solib_c_init(void)
{
	ready = 42;
	printf("solib-c: constructor ran\n");
	fflush(stdout);
}

__attribute__((destructor)) static void solib_c_fini(void)
{
	printf("solib-c: destructor ran\n");
	fflush(stdout);
}

int solib_c_ready(void)
{
	return ready;
}

const char *solib_c_word(int i)
{
	return i >= 0 && i < 3 ? words[i] : "?";
}

long long solib_c_div(long long a, long long b)
{
	return a / b;
}

int solib_c_fmt(char *buf, int len, int v)
{
	return snprintf(buf, len, "v=%d", v);
}
