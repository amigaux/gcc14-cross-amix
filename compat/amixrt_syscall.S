/* amixrt_syscall.S -- the system-call entry libamixrt.so's functions use.
 *
 *	int __amixrt_syscall(int number, ...);
 *
 * AMIX enters the kernel with trap #0, the call number in %d0 and the
 * arguments on the stack above a return address, as the C caller left them.
 * The kernel sets the carry flag on failure, with the error number in %d0.
 * This drops the number from the stack by moving the return address over it,
 * traps, and puts the return address back where the caller expects to pop
 * its arguments from.  A failure goes to libc.so.1's _cerror, which stores
 * the error in errno -- the program's, once the runtime linker has rebound
 * libc -- and returns -1.  The jump is through the PLT and the symbol is
 * hidden, so the text needs no relocation and nothing outside the library can
 * call it.  The result is copied to %a0 too, the AMIX convention for a
 * function that may return a pointer.
 */
	.text
	.globl	__amixrt_syscall
	.hidden	__amixrt_syscall
	.type	__amixrt_syscall, @function
__amixrt_syscall:
	movel	%sp@(4), %d0
	movel	%sp@+, %sp@
	trap	#0
	bccs	1f
	movel	%sp@, %sp@-
	jra	_cerror@PLTPC
1:	movel	%sp@, %sp@-
	moveal	%d0, %a0
	rts
	.size	__amixrt_syscall, .-__amixrt_syscall
