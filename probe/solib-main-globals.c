/* solib-main-globals.c -- a program that uses a shared library's exported
 * record directly, as a widget subclass uses libXt's class records.
 *
 * Linked against libsolibglobals.so.1 by tools/test-pic-globals.sh.  The
 * program refers to rec_exported, so ld gives it a copy (R_68K_COPY).  Exit
 * status 0 means the library's pointer to the record and the program's
 * address of it are the same object, and the copy holds the library's
 * initial values.
 */
#include <stdio.h>

struct rec {
	void (*f)(void);
	int n;
};

extern struct rec rec_exported;
struct rec *lib_ptr_exported(void);
int lib_static_n(void);

int main(void)
{
	struct rec *lib = lib_ptr_exported();
	int same = lib == &rec_exported;
	int ok = same && rec_exported.n == 5 && lib->n == 5
		 && rec_exported.f != 0 && lib_static_n() == 13;

	printf("main: &rec_exported=%p library's pointer=%p (%s) n=%d (5)"
	       " static+hidden=%d (13) -> %s\n",
	       (void *)&rec_exported, (void *)lib, same ? "same" : "DIFFERENT",
	       rec_exported.n, lib_static_n(), ok ? "PASS" : "FAIL");
	fflush(stdout);
	return ok ? 0 : 1;
}
