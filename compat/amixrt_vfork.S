/* amixrt_vfork.S -- vfork(2), for libamixrt.so.
 *
 *	pid_t vfork(void);
 *
 * libc.so.1 does not export vfork; AMIX has it only as a static member of
 * usr/ccs/lib/libc.so.  ToolTalk starts ttsession with it.  It cannot be a C
 * function, or a call through __amixrt_syscall: the child runs on the
 * parent's stack until it execs or exits, so anything the child pushes or
 * pops below the caller's frame -- a return address, a saved register --
 * is gone or changed when the parent resumes in the same frame.
 *
 * So this is the entry libc's member has, instruction for instruction, with
 * nothing on the stack of its own.  The return address is kept in %a0 across
 * the trap: registers are the one thing child and parent do not share.
 * SYS_vfork is 119.  The kernel returns the child's pid in %d0 in both
 * processes and tells them apart by %d1, non-zero in the child, which
 * returns 0.  Each puts its return address back on the stack before the
 * rts, since the other may have written over it.
 *
 * A failure, when there is no child, branches to __amixrt_cerror
 * (amixrt_syscall.S) with the error in %d0 and the caller's return address
 * on the stack, where libc's member branches to _cerror.  Not to _cerror
 * through the PLT: the runtime linker's lazy binding would return with
 * _cerror's address in %d0, and errno would get that.  That branch's target
 * is the only difference from libc's code, and tools/test-amixrt.sh compares
 * the rest byte for byte.
 */
	.text
	.globl	vfork
	.type	vfork, @function
vfork:
	moveal	%sp@, %a0
	moveq	#119, %d0
	trap	#0
	bccl	1f
	bral	__amixrt_cerror
1:	cmpil	#0, %d1
	beql	2f
	moveq	#0, %d0
2:	movel	%a0, %sp@
	rts
	.size	vfork, .-vfork
