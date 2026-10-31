/* cxa.c -- does libamixc99's __cxa_atexit run the right destructors, in the
 * right order, at the right time?
 *
 * Host-only: tools/test-compat-host.sh builds compat/amix_cxa.c natively with
 * its three symbols renamed, so the host C library's own __cxa_atexit is left
 * alone.  The logic is plain C, and the questions are about which entries a
 * call runs, which it leaves, and in what order -- none of which needs the
 * Amiga.
 *
 * At exit the question is the order against stdio's _cleanup, which AMIX's
 * crt1.o registers with atexit just before it runs the program's constructors,
 * after every shared library's DT_INIT has run.  The constructors at
 * priorities 99 and 100 below stand in for those two steps: a "library"
 * registers an entry first, then "_cleanup" is registered.  The exit hook must
 * still run before it.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int amix_test_cxa_atexit(void (*func)(void *), void *arg, void *dso);
void amix_test_cxa_finalize(void *dso);

static int fails;
static char trail[64];

static void step(void *arg)
{
	strcat(trail, (const char *) arg);
}

static void expect(const char *what, const char *want)
{
	int ok = strcmp(trail, want) == 0;

	if (!ok) fails++;
	printf("%-48s %-5s ran \"%s\" want \"%s\"\n", what, ok ? "ok" : "FAIL", trail, want);
	fflush(stdout);
	trail[0] = 0;
}

/* Two shared objects' handles; the program's is 0, as crtbegin.o's is. */
static char object_a, object_b;
#define A ((void *) &object_a)
#define B ((void *) &object_b)

static void registers_more(void *arg)
{
	(void) arg;
	strcat(trail, "x");
	amix_test_cxa_atexit(step, "y", 0);
}

static void finalizes_b(void *arg)
{
	(void) arg;
	strcat(trail, "c");
	amix_test_cxa_finalize(B);
}

/* ---- exit order ---- */
static char at_exit[16];

static void exit_step(void *arg)
{
	strcat(at_exit, (const char *) arg);
}

static void fake_cleanup(void)
{
	int ok = strcmp(at_exit, "PL") == 0;

	if (!ok) fails++;
	printf("%-48s %-5s ran \"%s\" want \"PL\"\n",
	       "exit: every destructor before _cleanup", ok ? "ok" : "FAIL", at_exit);
	printf("%s\n", fails ? "FAILED" : "all passed");
	fflush(stdout);
	_exit(fails ? 1 : 0);
}

static void nothing(void *arg)
{
	(void) arg;
}

/* The first __cxa_atexit call, from a "library", before _cleanup exists. */
__attribute__((constructor(99))) static void library_init(void)
{
	amix_test_cxa_atexit(nothing, 0, A);
}

__attribute__((constructor(100))) static void crt1_registers_cleanup(void)
{
	atexit(fake_cleanup);
}

int main(void)
{
	amix_test_cxa_atexit(step, "1", B);
	amix_test_cxa_atexit(step, "p", 0);
	amix_test_cxa_atexit(step, "2", B);
	amix_test_cxa_atexit(step, "q", 0);
	amix_test_cxa_finalize(B);
	expect("finalize(B) runs B's entries, newest first", "21");
	amix_test_cxa_finalize(B);
	expect("finalize(B) again runs nothing", "");

	amix_test_cxa_atexit(step, "3", B);
	amix_test_cxa_atexit(finalizes_b, 0, A);
	amix_test_cxa_finalize(A);
	expect("a destructor may finalize another object", "c3");

	amix_test_cxa_atexit(registers_more, 0, 0);
	amix_test_cxa_finalize(0);
	/* Everything left: the "x" entry, the "y" it adds, then q, p and the
	 * library's entry from library_init, which logs nothing. */
	expect("finalize(0) runs everything left, newest first", "xyqp");

	/* Left for the exit hook: a library's entry and the program's. */
	amix_test_cxa_atexit(exit_step, "L", A);
	amix_test_cxa_atexit(exit_step, "P", 0);
	fflush(stdout);
	return 0;
}
