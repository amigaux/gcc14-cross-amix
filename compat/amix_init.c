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

/* Three things differ from the executable's copy above.
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

AMIX_HIDDEN void __amix_run_fini_array(void)
{
	void (**p)(void);

	/* Reverse order, as destruction should be. */
	for (p = __amix_fini_array_end; p > __amix_fini_array_start; ) {
		p--;
		if (*p != 0) (**p)();
	}
}

/* crtbeginS.o's .fini fragment calls __cxa_finalize(__dso_handle), if it can
 * find one, to run this object's C++ static destructors before the object goes
 * away.  The executable exports libamixc99's, and that one pops the whole list,
 * running the entries of the object it is given and dropping everyone else's:
 * a library that calls it at exit loses the destructors of every object
 * registered after its own.  This weak, hidden no-op takes its place inside
 * the object, so its static destructors run from libamixc99's exit hook along
 * with everyone else's, while it is still loaded.  The cost: dlclose does not
 * run them, so a library with C++ static objects must stay loaded until exit. */
AMIX_HIDDEN __attribute__((weak)) void __cxa_finalize(void *dso)
{
	(void) dso;
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
	"	bsr.l __amix_run_init_array\n"
	"	.section .fini,\"ax\",@progbits\n"
	"	bsr.l __amix_run_fini_array\n"
	"	.text\n"
);

#endif /* __PIC__ */
