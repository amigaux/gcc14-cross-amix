/* amix_dso.c -- run shared libraries' constructors once the program has
 * started.
 *
 * AMIX's runtime linker calls each library's DT_INIT from _rtld, before it
 * returns to the program's _start -- and before _rt_setup calls _rt_setaddr,
 * which points 25 of libc.so.1's own references at the program's copies:
 * __iob and the stdio buffers, errno, _environ, _fp_hw, __ctype, the timezone
 * variables, and malloc, free, calloc and realloc.  Until then libc works on
 * its own copies while the library already uses the program's.  A
 * constructor's printf fills libc's private stdout, its fflush flushes the
 * program's, and the text is never written -- measured on a 68030 and a
 * 68060.  getenv finds nothing either: crt1.o has not set _environ yet.
 *
 * So a library's DT_INIT (amix_initS.o) does not run its .init_array: it
 * hands it to __amix_dso_init, here in the program, which keeps the libraries
 * in the order the runtime linker initialised them.  The program's own
 * start-up then runs them from its .init_array, at priority 100: after libc
 * is consistent and _environ is set, after libstdc++'s standard streams
 * (priority 90), before the program's own constructors.  A library loaded
 * later, by dlopen, runs at once.
 *
 * Every library built with gcc -shared refers to __amix_dso_init, so linking
 * a program against one pulls this member out of libamixc99 and exports it.
 */

/* Priorities up to 100 are reserved for the implementation, which this is. */
#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"

struct amix_dso_init {
	struct amix_dso_init *next;
	void (*run)(void);
};

static struct amix_dso_init *amix_dso_first, *amix_dso_last;
static int amix_dso_started;

void __amix_dso_init(struct amix_dso_init *d)
{
	if (amix_dso_started) {
		d->run();
		return;
	}
	d->next = 0;
	if (amix_dso_last != 0) amix_dso_last->next = d;
	else amix_dso_first = d;
	amix_dso_last = d;
}

__attribute__((constructor(100))) static void amix_dso_run(void)
{
	struct amix_dso_init *d;

	amix_dso_started = 1;
	for (d = amix_dso_first; d != 0; d = d->next) d->run();
	amix_dso_first = amix_dso_last = 0;
}
