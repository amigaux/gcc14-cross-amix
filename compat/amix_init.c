/* amix_init.c -- run the .init_array, which AMIX's 1991 startup knows nothing
 * about.
 *
 * THE SYMPTOM: no C++ static constructor runs.  Every global object is left
 * zeroed, and the first thing that touches one dies -- for OpenTTD that is
 * std::_Rb_tree_increment, walking a driver-registry std::map that was never
 * constructed, with the crash log helpfully reporting "Blitter: none" and
 * "Video driver: none".
 *
 * THE CAUSE is a disagreement about a section name, thirty years apart.  AMIX's
 * crt1.o calls _init, crti.o provides it, and GCC's crtbegin.o puts a fragment
 * there that walks .ctors -- so far so 1991.  But the modern linker script
 * folds .ctors from ordinary objects INTO .init_array:
 *
 *     KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o ...) .ctors))
 *
 * so the constructors land in .init_array, .ctors keeps only crtbegin's -1 and
 * crtend's 0, and __do_global_ctors_aux walks an empty list and returns.  On a
 * system whose crt1 knows about .init_array nobody notices, because its startup
 * walks that array too.  AMIX's does not, and there is no diagnostic anywhere:
 * it links, it runs, and the constructors are simply skipped.
 *
 * THE FIX is this object, placed in the startfile sequence right after
 * crtbegin.o, contributing its own .init fragment.  Ordering is why it is a
 * separate object named in the compiler's specs rather than a member of
 * libamixc99: a library member is pulled in only when something references it,
 * and nothing references a constructor.
 *
 * SHARED OBJECTS need the same walk, from DT_INIT and DT_FINI: AMIX's runtime
 * linker calls those for every object it loads, and has never heard of
 * DT_INIT_ARRAY either.  The same file built with -fPIC is amix_initS.o, which
 * the specs link into `gcc -shared` output instead -- see the second half.
 * The program's copy also holds the runner that libraries hand their walks to,
 * so that they run once libc is consistent -- see "Shared libraries'
 * constructors" below.
 */

#ifndef __PIC__

/* Provided by the linker script, and weak so this still links if a future
 * script stops emitting them. */
extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));
extern void (*__fini_array_start[])(void) __attribute__((weak));
extern void (*__fini_array_end[])(void) __attribute__((weak));

void __amix_run_init_array(void)
{
	void (**p)(void);

	if (__init_array_start == 0) return;
	for (p = __init_array_start; p < __init_array_end; p++) {
		if (*p != 0) (**p)();
	}
}

void __amix_run_fini_array(void)
{
	void (**p)(void);

	if (__fini_array_start == 0) return;
	/* Reverse order, as destruction should be. */
	for (p = __fini_array_end; p > __fini_array_start; ) {
		p--;
		if (*p != 0) (**p)();
	}
}

/* Shared libraries' constructors, run once the program has started.
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
 * So a library's DT_INIT (amix_initS.o, below) does not run its .init_array:
 * it hands it to __amix_dso_init, which the program's start-up runs from its
 * .init_array at priority 100 -- after libc is consistent and _environ is
 * set, after libstdc++'s standard streams (priority 90), before the
 * program's own constructors.  A library loaded later, by dlopen, runs at
 * once.
 *
 * Last in, first out.  The runtime linker loads objects breadth first and
 * calls their DT_INIT in load order, so a library comes before the libraries
 * it NEEDs: libXmu before libXt.  Running the list backwards initialises a
 * library's dependencies before the library, as an ELF system does.
 *
 * It lives in this startfile, linked into every program, rather than in
 * libamixc99: a library the program reaches only through another library's
 * NEEDED must still find it, and ld searches archives before it reads such a
 * library.  ld exports it whenever a shared library in the link refers to it,
 * directly or not.  A program that uses no shared library of ours carries
 * these few bytes and never calls them. */

/* Priorities up to 100 are reserved for the implementation, which this is. */
#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"

struct amix_dso_init {
	struct amix_dso_init *next;
	void (*run)(void);
};

static struct amix_dso_init *amix_dso_list;
static int amix_dso_started;

void __amix_dso_init(struct amix_dso_init *d)
{
	if (amix_dso_started) {
		d->run();
		return;
	}
	d->next = amix_dso_list;
	amix_dso_list = d;
}

__attribute__((constructor(100))) static void amix_dso_run(void)
{
	struct amix_dso_init *d;

	amix_dso_started = 1;
	while ((d = amix_dso_list) != 0) {
		amix_dso_list = d->next;
		d->run();
	}
}

/* The fragments themselves.  crti.o opens _init and _fini, crtn.o closes them,
 * and everything linked between contributes its body -- which is why this has
 * to sit inside that window and why the specs decide where. */
__asm__(
	"	.section .init,\"ax\",@progbits\n"
	"	jsr __amix_run_init_array\n"
	"	.section .fini,\"ax\",@progbits\n"
	"	jsr __amix_run_fini_array\n"
	"	.text\n"
);

#else /* __PIC__: amix_initS.o, for shared objects */

/* Four things differ from the executable's copy above.
 *
 * The linker script for -shared defines no __init_array_start and friends: a
 * modern dynamic linker reads DT_INIT_ARRAY instead, so nothing needed them.
 * This object brackets the arrays itself.  It is an endfile, linked after every
 * object and library, so its end labels in plain .init_array and .fini_array
 * land after everyone else's entries.  Its start labels go in .init_array.0 and
 * .fini_array.0, which the script's SORT_BY_INIT_PRIORITY places ahead of every
 * other entry: priority 0 is reserved, and nothing else uses it.  Each start
 * label carries a null entry, which the walk skips.  Without it an object with
 * no constructors would have an empty .init_array, and ld warns about that on
 * every link.
 *
 * Every symbol is hidden, so each shared object walks its own arrays and
 * exports nothing that could take the place of the executable's copy.
 *
 * The calls from the .init and .fini fragments are PC-relative, so the text
 * needs no relocation at load time.
 *
 * The walk does not run from DT_INIT itself.  AMIX's runtime linker calls
 * DT_INIT before libc.so.1 is consistent with the program -- stdio, errno,
 * _environ and malloc among others are rebound only afterwards -- so the
 * .init fragment hands the walk to __amix_dso_init in the program, which runs
 * it during the program's start-up; the first half of this file has the
 * details.  The reference is weak.  A program linked by something else has no
 * __amix_dso_init, and neither does one of ours that loads the library with
 * dlopen without having linked any shared library of ours, because ld then
 * had no reason to export it.  The runtime linker resolves the weak reference
 * to 0, and the library runs its own array at once: right for dlopen, which
 * comes after the program's start-up, and the best that can be done for a
 * foreign program.
 *
 * Being an endfile still puts the .init fragment after crtbeginS.o's, the same
 * order as in an executable. */
#define AMIX_HIDDEN __attribute__((visibility("hidden")))

extern void (*__amix_init_array_start[])(void) AMIX_HIDDEN;
extern void (*__amix_init_array_end[])(void) AMIX_HIDDEN;
extern void (*__amix_fini_array_start[])(void) AMIX_HIDDEN;
extern void (*__amix_fini_array_end[])(void) AMIX_HIDDEN;

AMIX_HIDDEN void __amix_run_init_array(void)
{
	void (**p)(void);

	for (p = __amix_init_array_start; p < __amix_init_array_end; p++) {
		if (*p != 0) (**p)();
	}
}

struct amix_dso_init {
	struct amix_dso_init *next;
	void (*run)(void);
};

/* In the program: amix_init.o, above. */
extern void __amix_dso_init(struct amix_dso_init *) __attribute__((weak));

static struct amix_dso_init amix_this_object = { 0, __amix_run_init_array };

AMIX_HIDDEN void __amix_defer_init_array(void)
{
	if (__amix_dso_init != 0) __amix_dso_init(&amix_this_object);
	else __amix_run_init_array();
}

AMIX_HIDDEN void __amix_run_fini_array(void)
{
	void (**p)(void);

	/* Reverse order, as destruction should be. */
	for (p = __amix_fini_array_end; p > __amix_fini_array_start; ) {
		p--;
		if (*p != 0) (**p)();
	}
}

#define AMIX_LABEL(sec, name) \
	"	.section " sec ",\"aw\"\n" \
	"	.p2align 2\n" \
	"	.globl " name "\n" \
	"	.hidden " name "\n" \
	name ":\n"

__asm__(
	AMIX_LABEL(".init_array.0", "__amix_init_array_start")
	"	.long 0\n"
	AMIX_LABEL(".init_array", "__amix_init_array_end")
	AMIX_LABEL(".fini_array.0", "__amix_fini_array_start")
	"	.long 0\n"
	AMIX_LABEL(".fini_array", "__amix_fini_array_end")
	"	.section .init,\"ax\",@progbits\n"
	"	bsr.l __amix_defer_init_array\n"
	"	.section .fini,\"ax\",@progbits\n"
	"	bsr.l __amix_run_fini_array\n"
	"	.text\n"
);

#endif /* __PIC__ */
