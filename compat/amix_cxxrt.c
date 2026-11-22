/* amix_cxxrt.c -- linked into libstdc++.so.6, the shared C++ runtime, and
 * nowhere else.  toolchain/build-cxx-shared.sh builds it with -fPIC.  Every
 * symbol here is hidden, so the library exports nothing from this file.
 *
 * ONE RUNTIME PER PROCESS.  A program linked with `g++ -shared-libgcc` takes
 * operator new, __cxa_throw, the personality routine, the exception globals
 * and the unwinder with its registry of .eh_frame sections from
 * libstdc++.so.6, as every C++ library linked with `g++ -shared` does, so an
 * exception can be thrown in one object and caught in another.  A program
 * linked the default way carries static copies of all of those instead, and
 * ld exports them as soon as a library it loads refers to them.  AMIX's
 * runtime linker looks in the program first, so a library's __cxa_throw then
 * runs the program's copy, whose unwinder is hidden in the program and has
 * never seen the library's .eh_frame: the first exception thrown in a library
 * ends in std::terminate, with nothing to say why.
 *
 * amix_cxxrt_check says why, at start-up, before anything can throw.  The
 * linker gives this library's own definitions a second, hidden name (a
 * HIDDEN() assignment in build-cxx-shared.sh), and references to those bind
 * at link time.  The plain names go through the GOT and bind at run time to
 * whatever the runtime linker finds first.  If the two differ, the program
 * has a C++ runtime of its own, and the process stops with a message naming
 * the fix.
 *
 * It runs from .init_array, so the library's DT_INIT hands it to the
 * program's runner (compat/amix_init.c) like every other constructor: after
 * the runtime linker has bound every symbol, before the program's
 * constructors and main.  Nothing it calls needs libc to be consistent:
 * write and _exit are system calls.
 *
 * Below it are floor and frexpl, which libstdc++ needs and the library must
 * not take from elsewhere.  tools/test-compat-host.sh builds them for the
 * host (AMIX_CXXRT_HOST_TEST, without the check) and compares them with the
 * host's own.
 */

#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"

#define AMIX_HIDDEN __attribute__((visibility("hidden")))

#ifndef AMIX_CXXRT_HOST_TEST

extern int write(int, const void *, unsigned);
extern void _exit(int);

/* The plain names, bound by the runtime linker. */
extern void __gxx_personality_v0(void);
extern void __cxa_throw(void);
extern void __cxa_begin_catch(void);

/* This library's own definitions of the same (build-cxx-shared.sh). */
extern char __amix_cxxrt_own_personality[] AMIX_HIDDEN;
extern char __amix_cxxrt_own_throw[] AMIX_HIDDEN;
extern char __amix_cxxrt_own_begin_catch[] AMIX_HIDDEN;

static const char amix_cxxrt_msg[] =
	"libstdc++.so.6: this program has a C++ runtime of its own (static "
	"libstdc++) and also loads the shared one; exceptions cannot cross "
	"between them.  Link the program with g++ -shared-libgcc.\n";

/* Two different names are two different objects to the compiler, which
 * folds a comparison of their addresses to "unequal" and then writes the
 * message every time.  The empty asm hides where the pointer came from. */
static int amix_same(const void *bound, const void *own)
{
	__asm__ ("" : "+r" (bound));
	return bound == own;
}

/* Priority 80: first among this library's constructors (the standard
 * streams are at 90).  Priorities up to 100 belong to the implementation. */
__attribute__((constructor(80))) AMIX_HIDDEN void amix_cxxrt_check(void)
{
	if (amix_same((const void *) &__gxx_personality_v0, __amix_cxxrt_own_personality)
	    && amix_same((const void *) &__cxa_throw, __amix_cxxrt_own_throw)
	    && amix_same((const void *) &__cxa_begin_catch, __amix_cxxrt_own_begin_catch))
		return;
	write(2, amix_cxxrt_msg, sizeof amix_cxxrt_msg - 1);
	_exit(127);
}

#endif /* !AMIX_CXXRT_HOST_TEST */

/* floor, for libstdc++'s hash tables (std::unordered_map's rehash policy
 * calls it).  AMIX has floor only in libm.a, which is not position-
 * independent: linked into this library it would make text relocations, and
 * left undefined it would pull libm's copy into every program that links
 * libstdc++.so.6.  This one is hidden: libstdc++ calls it and nothing else
 * sees it.  It rounds toward minus infinity by clearing the fraction bits
 * below the binary point, after adding just under one unit to a negative
 * number.  Integral values, infinities and NaNs come back as they are, and
 * the sign of zero is kept.  tools/test-compat-host.sh checks it against
 * the host's floor. */
AMIX_HIDDEN double floor(double x)
{
	union { double d; unsigned long long u; } v;
	unsigned long long m;
	int e;

	v.d = x;
	e = (int) ((v.u >> 52) & 0x7ff) - 0x3ff;
	if (e >= 52)
		return x;
	if (e < 0) {
		if ((v.u << 1) == 0)
			return x;
		return (v.u >> 63) ? -1.0 : 0.0;
	}
	m = 0x000fffffffffffffULL >> e;
	if ((v.u & m) == 0)
		return x;
	if (v.u >> 63)
		v.u += m;
	v.u &= ~m;
	return v.d;
}

/* frexpl, for std::hash<long double>.  SVR4.0 has no long double functions;
 * libstdc++'s own stub for this one (math_stubs_long_double.cc) is the same
 * line, but that file also wraps eighteen functions of libm, so it stays out
 * of the shared library (in libstdc++_nonshared.a, for programs that call
 * them) and this hidden copy takes its place.  frexp is in libc.so.1. */
extern double frexp(double, int *);

AMIX_HIDDEN long double frexpl(long double x, int *exp)
{
	return frexp((double) x, exp);
}
