/* amix_cxa.c -- the C++ ABI entry points AMIX's libc predates.
 *
 * The Itanium C++ ABI says the C library provides __cxa_atexit: the compiler
 * emits a call to it for every object with static storage duration and a
 * destructor, and the runtime calls those destructors at exit in reverse order.
 * AMIX is SVR4.0 and knows only atexit(void (*)(void)), which cannot carry the
 * object pointer, so libstdc++ does not link:
 *
 *     system_error.cc:145: undefined reference to `__cxa_atexit'
 *
 * The alternative was to configure GCC with --disable-__cxa_atexit, which makes
 * it fall back to the older scheme. This is preferred because the gap is in the
 * C library, not in the compiler: filling it here keeps the compiler standard,
 * and anything else built with it gets working static destructors too.
 *
 * The list grows on demand rather than being a fixed table, because silently
 * dropping a destructor past some arbitrary count is the kind of bug that only
 * shows up as a file not flushed on a machine nobody can attach a debugger to.
 */
#include <stdlib.h>

typedef void (*amix_cxa_func)(void *);

struct amix_cxa_entry {
	amix_cxa_func func;
	void *arg;
	void *dso;
};

static struct amix_cxa_entry *amix_cxa_list;
static unsigned amix_cxa_count;
static unsigned amix_cxa_capacity;
static int amix_cxa_hooked;

/* Address of this translation unit, for the ABI's dso argument. Normally
 * supplied by crtbegin; defined weakly here so a toolchain that does provide
 * one wins.  Not in libamixrt.so, the PIC build: each shared object has its
 * own, hidden, from crtbeginS.o. */
#ifndef __PIC__
void *__dso_handle __attribute__((weak)) = &__dso_handle;
#endif

/* Run and remove the entries registered for one object, newest first, or
 * every entry when dso is 0 -- the exit hook below, or a caller that wants
 * them all.  A shared library's DT_FINI calls this with its own handle, through
 * crtbeginS.o, at exit and when dlclose unloads it; entries of other objects
 * stay where they are.  The search starts again from the newest entry after
 * every call, because a destructor may register more work or finalize another
 * object. */
void __cxa_finalize(void *dso)
{
	for (;;) {
		struct amix_cxa_entry e;
		unsigned i = amix_cxa_count, j;

		while (i > 0 && dso != 0 && amix_cxa_list[i - 1].dso != dso) i--;
		if (i == 0) return;
		e = amix_cxa_list[i - 1];
		for (j = i; j < amix_cxa_count; j++) amix_cxa_list[j - 1] = amix_cxa_list[j];
		amix_cxa_count--;
		e.func(e.arg);
	}
}

static void amix_cxa_run_all(void)
{
	__cxa_finalize(0);
}

/* The exit hook has to run before stdio's _cleanup, or a destructor's output
 * is buffered after the last flush and lost.  exit() runs atexit handlers
 * newest first, and AMIX's crt1.o registers _cleanup just before it calls
 * _init -- so the hook is registered from the program's own .init_array,
 * which _init walks, with the first priority a program may use, ahead of
 * every ordinary constructor.  Registering it on the first __cxa_atexit
 * instead is too early when that call comes from a shared library: the
 * runtime linker runs libraries' DT_INIT before the program starts, and the
 * hook would then run after _cleanup.  __cxa_atexit still registers one if
 * this has not run yet, so destructors run even in a program started some
 * other way; the second registration finds the list empty.
 *
 * libamixrt.so, the PIC build, has no constructor: the library initialises
 * nothing of its own.  Its copy is used only when the program has none, and
 * then the first __cxa_atexit call comes from a library's constructor, which
 * the program's start-up runs after crt1.o has registered _cleanup -- so
 * registering the hook there is late enough. */
#ifndef __PIC__
__attribute__((constructor(101))) static void amix_cxa_hook(void)
{
	if (atexit(amix_cxa_run_all) == 0) amix_cxa_hooked = 1;
}
#endif

int __cxa_atexit(amix_cxa_func func, void *arg, void *dso)
{
	if (func == 0) return -1;

	if (!amix_cxa_hooked) {
		if (atexit(amix_cxa_run_all) != 0) return -1;
		amix_cxa_hooked = 1;
	}

	if (amix_cxa_count == amix_cxa_capacity) {
		unsigned grown = amix_cxa_capacity ? amix_cxa_capacity * 2 : 32;
		struct amix_cxa_entry *bigger = (struct amix_cxa_entry *)
			realloc(amix_cxa_list, grown * sizeof(*bigger));
		if (bigger == 0) return -1;
		amix_cxa_list = bigger;
		amix_cxa_capacity = grown;
	}

	amix_cxa_list[amix_cxa_count].func = func;
	amix_cxa_list[amix_cxa_count].arg = arg;
	amix_cxa_list[amix_cxa_count].dso = dso;
	amix_cxa_count++;
	return 0;
}
