/* retabi.c -- do GCC 14 and AMIX agree on how a function returns its value?
 *
 * Arguments were checked long ago (interop.c); returns were not, and they
 * differ twice between AMIX and an unmodified m68k-elf GCC:
 *
 *   pointer  AMIX returns it in %a0 and copies it to %d0, and the caller
 *            reads %a0.  m68k-elf returns it in %d0 only.
 *   struct   AMIX returns every struct in memory, through a buffer whose
 *            address the caller passes in %a0.  m68k-elf returns 4- and
 *            8-byte structs in %d0/%d1.
 *
 * Both fail silently: the caller reads a register that holds something else.
 * toolchain/amix-abi.h fixes them; docs/TOOLCHAIN.md section 4 has the story.
 *
 * The other half, retabi-period.c, is built by gcc 2.7.2.3:
 *
 *   $AMIX_CROSS_BIN/m68k-cbm-sysv4-gcc -O2 -c -o probe/retabi-period.o probe/retabi-period.c
 *   tools/amix-cc.sh -O2 -o probe/retabi probe/retabi.c probe/retabi-period.o
 *
 * Each line is flushed before its step runs, because a struct returned the
 * wrong way is written through whatever %a0 held: the probe may die rather
 * than print WRONG, and the last line then names the step. */
#include <stdio.h>
#include <stdlib.h>

struct pair { int q, r; };

extern char *old_ptr(char *, int);
extern struct pair old_pair(int, int);
extern int old_calls_ptr(char *, int);
extern int old_calls_pair(int, int);

char *g14_ptr(char *p, int n)
{
	return p + n;
}

struct pair g14_pair(int a, int b)
{
	struct pair r;

	r.q = a / b;
	r.r = a % b;
	return r;
}

static char buf[16];
static int failed;

static void step(const char *what)
{
	printf("%-38s", what);
	fflush(stdout);
}

static void verdict(int ok)
{
	printf("%s\n", ok ? "ok" : "WRONG");
	fflush(stdout);
	failed += !ok;
}

int main(void)
{
	struct pair r;
	div_t d;
	ldiv_t ld;

	step("pointer, gcc 2.7.2.3 -> GCC 14:");
	verdict(old_ptr(buf, 5) == buf + 5);
	step("pointer, GCC 14 -> gcc 2.7.2.3:");
	verdict(old_calls_ptr(buf, 5));

	step("struct, gcc 2.7.2.3 -> GCC 14:");
	r = old_pair(17, 5);
	verdict(r.q == 3 && r.r == 2);
	step("struct, GCC 14 -> gcc 2.7.2.3:");
	verdict(old_calls_pair(17, 5));

	step("div_t, libc div() -> GCC 14:");
	d = div(17, 5);
	verdict(d.quot == 3 && d.rem == 2);
	step("ldiv_t, libc ldiv() -> GCC 14:");
	ld = ldiv(-17L, 5L);
	verdict(ld.quot == -3 && ld.rem == -2);

	printf("done, %d wrong\n", failed);
	return failed != 0;
}
