/* solib-globals.c -- a shared library whose data points at its own exported
 * data, the way libXt's coreWidgetClass points at widgetClassRec.
 *
 * Built by tools/test-pic-globals.sh with `gcc -shared -fPIC`.  An executable
 * that uses rec_exported gets its own copy of it (R_68K_COPY), and every
 * reference to rec_exported -- the executable's and this library's -- has to
 * end up at that copy.  For ptr_exported that only works if the object file
 * keeps its relocation against the symbol rec_exported: the runtime linker
 * then looks the symbol up and finds the executable's copy first.  A
 * relocation against the section becomes R_68K_RELATIVE and keeps pointing at
 * this library's own copy, which the program never sees.
 *
 * The static and hidden records are the opposite case: nothing outside the
 * library can refer to them, so their pointers stay relative.
 */
struct rec {
	void (*f)(void);
	int n;
};

void rec_fn(void)
{
}

struct rec rec_exported = { rec_fn, 5 };
struct rec *ptr_exported = &rec_exported;

static struct rec rec_static = { rec_fn, 6 };
struct rec *ptr_static = &rec_static;

__attribute__((visibility("hidden"))) struct rec rec_hidden = { rec_fn, 7 };
struct rec *ptr_hidden = &rec_hidden;

/* The library's own view of its exported record. */
struct rec *lib_ptr_exported(void)
{
	return ptr_exported;
}

int lib_static_n(void)
{
	return ptr_static->n + ptr_hidden->n;
}
