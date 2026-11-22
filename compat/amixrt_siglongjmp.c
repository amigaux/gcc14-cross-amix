/* amixrt_siglongjmp.c -- siglongjmp, for libamixrt.so.
 *
 * libc.so.1 exports sigsetjmp but not siglongjmp, which AMIX has only as a
 * static member of usr/ccs/lib/libc.so.  ToolTalk uses the pair.  This one
 * has to undo what libc.so.1's sigsetjmp did, so it follows that function's
 * buffer, read off its code: a sigjmp_buf is a ucontext_t (<setjmp.h> sizes
 * it so), which sigsetjmp fills with getcontext(2) and then corrects --
 * the stack pointer to the caller's, after the return, and the program
 * counter to the return address -- and in which it clears UC_SIGMASK.
 * libc's siglongjmp then stores the value sigsetjmp is to return in the
 * saved %d0, 1 for 0, and resumes the context with setcontext(2), which
 * libc.so.1 exports.  This does the same.
 *
 * setcontext does not return unless the buffer is bad.  libc's sigsetjmp
 * clears UC_SIGMASK whatever its second argument says, so neither libc's
 * siglongjmp nor this one restores the signal mask: the behaviour of the
 * pair on AMIX, and the one a program linked with libc's member gets.
 */
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/ucontext.h>
#include <setjmp.h>
#include <stddef.h>

extern int setcontext(ucontext_t *);

/* The offsets libc.so.1's sigsetjmp writes: %d0 at 36, %sp at 96, %pc at
 * 100.  A header that disagreed would make this write the wrong word. */
_Static_assert(offsetof(ucontext_t, uc_mcontext.gregs[R_D0]) == 36, "gregs[R_D0]");
_Static_assert(offsetof(ucontext_t, uc_mcontext.gregs[R_SP]) == 96, "gregs[R_SP]");
_Static_assert(offsetof(ucontext_t, uc_mcontext.gregs[R_PC]) == 100, "gregs[R_PC]");
_Static_assert(sizeof(sigjmp_buf) >= sizeof(ucontext_t), "sigjmp_buf");

void siglongjmp(sigjmp_buf env, int val)
{
	ucontext_t *uc = (ucontext_t *)env;

	uc->uc_mcontext.gregs[R_D0] = val ? val : 1;
	setcontext(uc);
}
