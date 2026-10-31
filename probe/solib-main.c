/* solib-main.c -- a C program linked against the two test libraries.
 *
 * env=1,1 means each library's constructor saw the environment, so it ran
 * after the program's start-up rather than from the runtime linker.
 *
 * Built the ordinary way, `gcc -o solib-main solib-main.c -lsolibc -lsolibcxx`
 * against the shared objects from tools/test-shared.sh, and run on AMIX with
 * LD_LIBRARY_PATH pointing at them.  Exit status 0 means every value came back
 * as the library's constructor and data left it; the constructor and
 * destructor lines show when each ran.
 */
#include <stdio.h>
#include <string.h>

int solib_c_ready(void);
const char *solib_c_word(int);
long long solib_c_div(long long, long long);
int solib_c_fmt(char *, int, int);
int solib_cxx_value(void);
int solib_c_env(void);
int solib_cxx_env(void);

int main(void)
{
	char buf[16];
	volatile long long n = 600000000000LL, d = 7;
	int ready = solib_c_ready();
	int value = solib_cxx_value();
	const char *word = solib_c_word(2);
	long long q = solib_c_div(n, d);
	int env_c = solib_c_env(), env_cxx = solib_cxx_env();
	int ok;

	solib_c_fmt(buf, sizeof buf, 9);
	ok = ready == 42 && value == 7 && strcmp(word, "two") == 0
	     && q == 85714285714LL && strcmp(buf, "v=9") == 0
	     && env_c == 1 && env_cxx == 1;
	printf("main: ready=%d (42) value=%d (7) word=%s (two) div=%s fmt=%s (v=9)"
	       " env=%d,%d (1,1) -> %s\n",
	       ready, value, word, q == 85714285714LL ? "ok" : "WRONG", buf,
	       env_c, env_cxx, ok ? "PASS" : "FAIL");
	fflush(stdout);
	return ok ? 0 : 1;
}
