| A lower-bound compare (signed bge) before the real bound (unsigned bcs):
| the bound is the compare before the unsigned branch, 33 entries, not 2
| (pth's __pth_sched_eventmanager).
| expect: rc=1 findings=1
	.text
	.globl	sw
sw:	move.l	4(%sp),%d0
	move.l	8(%sp),%a0
	moveq	#32,%d2
	moveq	#1,%d4
	cmp.l	%d0,%d4
	bge	.Ldef
	cmp.l	%d0,%d2
	bcs	.Ldef
	add.l	%d0,%d0
	move.w	.Ltab(%pc,%d0.l),%d0
	jmp	.Ltab(%pc,%d0.w)
.Ltab:	.rept	33
	.word	0x03d0
	.endr
	.fill	(0x3d0 - 66) / 2, 2, 0x4e71
.Lc:	move.l	12(%sp),%d1
	bset	%d1,(%a0)
.Ldef:	rts
