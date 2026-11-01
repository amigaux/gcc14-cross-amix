/* solib-c.c -- a C shared library with a constructor and a destructor.
 *
 * Built by tools/test-shared.sh with `gcc -shared -fPIC`.  AMIX's runtime
 * linker runs a shared object's DT_INIT when it loads it and its DT_FINI at
 * exit, and knows nothing of .init_array -- so the constructor below runs only
 * because amix_initS.o hands the array from _init to the program, which walks
 * it during its own start-up.  The constructor reports itself twice: with
 * write(2), which works whenever it runs, and with stdio, which works only
 * once libc.so.1 is consistent with the program.
 *
 * Each function also exercises one thing a shared library has to get right:
 * initialised pointers (R_68K_RELATIVE), a 64-bit divide from libgcc (linked
 * into the object, hidden), and snprintf, which SVR4.0's libc does not have:
 * the library NEEDs libamixrt.so.1 for it, and the runtime linker binds it to
 * the program's own copy from libamixc99 when the program has one, as this
 * test's programs do.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int ready;
static int env_seen;
static const char *words[] = { "zero", "one", "two" };

__attribute__((constructor)) static void solib_c_init(void)
{
	static const char msg[] = "solib-c: constructor ran (write)\n";

	write(1, msg, sizeof msg - 1);
	ready = 42;
	env_seen = getenv("PATH") != 0;
	printf("solib-c: constructor ran (stdio)\n");
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

/* Whether the constructor saw the environment.  crt1.o sets it, so 1 means
 * the constructor ran after the program's start-up, as it should. */
int solib_c_env(void)
{
	return env_seen;
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
