| crt0.s -- entry point for running the test set under qemu-m68k user
| emulation.  Not AMIX code: qemu emulates Linux m68k system calls
| (trap #0, number in %d0, arguments in %d1-%d3), so exit is 1 and write 4.
	.text
	.globl	_start
_start:
	jsr	main
	move.l	%d0,%d1
	moveq	#1,%d0
	trap	#0
	.globl	sys_write
| int sys_write(int fd, const void *buf, unsigned len)
sys_write:
	move.l	%d2,-(%sp)
	move.l	%d3,-(%sp)
	move.l	12(%sp),%d1
	move.l	16(%sp),%d2
	move.l	20(%sp),%d3
	moveq	#4,%d0
	trap	#0
	move.l	(%sp)+,%d3
	move.l	(%sp)+,%d2
	rts
