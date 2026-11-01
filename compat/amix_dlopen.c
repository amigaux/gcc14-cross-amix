/* amix_dlopen.c -- dlopen that initialises a library's dependencies first.
 *
 * AMIX's runtime linker calls the DT_INIT of everything a dlopen loads in
 * load order: the opened library first, then the libraries it NEEDs.  A
 * library built here hands its constructors from DT_INIT to the program's
 * runner in amix_init.o, and inside a dlopen the runner only queues them;
 * when _dlopen returns, this runs the queue newest first, so a library's
 * dependencies are initialised before the library, as at start-up.
 *
 * The specs link every program and every shared library with --wrap=dlopen,
 * so their calls to dlopen arrive here as __wrap_dlopen.  A program takes
 * this from libamixc99, a shared library from libamixrt.so.1.  Both call the
 * runtime linker's _dlopen in libc.so.1 directly: libdl.so is only a set of
 * stubs that jump there.
 *
 * In libamixrt the runner is looked up weakly.  The specs export it from
 * every program built here; in a program built otherwise there is none, and
 * this is plain dlopen, with the runtime linker's order.
 */
extern void *_dlopen(char *, int);

struct amix_dso_init;
#ifdef __PIC__
extern struct amix_dso_init *__amix_dso_batch(struct amix_dso_init *, int)
	__attribute__((weak));
#else
extern struct amix_dso_init *__amix_dso_batch(struct amix_dso_init *, int);
#endif

void *__wrap_dlopen(char *path, int mode)
{
	struct amix_dso_init *mark;
	void *h;

#ifdef __PIC__
	if (__amix_dso_batch == 0) return _dlopen(path, mode);
#endif
	mark = __amix_dso_batch(0, 1);
	h = _dlopen(path, mode);
	__amix_dso_batch(mark, 0);
	return h;
}
