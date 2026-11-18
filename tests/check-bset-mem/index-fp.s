| The switch index in %fp (a6): jmp %pc@(T,%fp:w) (Xrtg's mfbSetScanline).
| expect: rc=1 findings=1
	.text
	.globl	sw
sw:	move.l	4(%sp),%d0
	move.l	8(%sp),%a0
	moveq	#3,%d1
	cmp.l	%d0,%d1
	bcs	.Ldef
	add.l	%d0,%d0
	movea.w	.Ltab(%pc,%d0.l),%a6
	jmp	.Ltab(%pc,%a6.w)
.Ltab:	.word	0x0190, 0x0190, 0x01d0, 0x01d0
	.fill	(0x190 - 8) / 2, 2, 0x4e71
.Lc0:	moveq	#0,%d0
	rts
	.fill	(0x1d0 - 0x194) / 2, 2, 0x4e71
.Lc1:	move.l	12(%sp),%d1
	bset	%d1,(%a0)
.Ldef:	rts
