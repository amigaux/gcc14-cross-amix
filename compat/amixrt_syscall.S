/* amixrt_syscall.S -- the system-call entry libamixrt.so's functions use,
 * and the error exit its assembly shares.
 *
 *	int __amixrt_syscall(int number, ...);
 *
 * AMIX enters the kernel with trap #0, the call number in %d0 and the
 * arguments on the stack above a return address, as the C caller left them.
 * The kernel sets the carry flag on failure, with the error number in %d0.
 * This drops the number from the stack by moving the return address over it,
 * traps, and puts the return address back where the caller expects to pop
 * its arguments from.  The result is copied to %a0 too, the AMIX convention
 * for a function that may return a pointer.
 *
 * A failure goes to __amixrt_cerror, which stores %d0 in errno and returns
 * -1 in %d0 and %a0, as libc.so.1's _cerror does.  It must not go to _cerror
 * through the PLT, as it once did: _cerror takes the error number in %d0,
 * and the first call through a PLT entry goes through the runtime linker's
 * lazy binding, which returns with the resolved address in %d0 -- so errno
 * got _cerror's address (0xC101FEE0 on AMIX) the first time a call failed.
 * errno is reached through the GOT instead, whose entry the runtime linker
 * fills when it loads the library: the program's errno when the program has
 * one, which libc.so.1 then uses too, else libc.so.1's.  Both symbols are
 * hidden, so they are reached PC-relative, the text needs no relocation,
 * and nothing outside the library can call them.  %a1 is the GOT pointer
 * here, a scratch register, where GCC would save and use %a5.
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
	bras	__amixrt_cerror
1:	movel	%sp@, %sp@-
	moveal	%d0, %a0
	rts
	.size	__amixrt_syscall, .-__amixrt_syscall

	.globl	__amixrt_cerror
	.hidden	__amixrt_cerror
	.type	__amixrt_cerror, @function
__amixrt_cerror:
	lea	(%pc, _GLOBAL_OFFSET_TABLE_@GOTPC), %a1
	movel	(errno@GOT, %a1), %a0
	movel	%d0, %a0@
	moveq	#-1, %d0
	moveal	%d0, %a0
	rts
	.size	__amixrt_cerror, .-__amixrt_cerror
