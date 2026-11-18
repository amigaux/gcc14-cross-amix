| The bound is "cmpa #N,%aX" (GCC 14 keeps the index in an address register).
| A label between the table and its cases (a stripped program's few
| symbols) must not end the table once its length is known.
| expect: rc=1 findings=1
	.text
	.globl	sw
sw:	move.l	4(%sp),%a1
	move.l	8(%sp),%a0
	cmpa.w	#3,%a1
	bhi	.Ldef
	move.l	%a1,%d0
	add.l	%d0,%d0
	move.w	.Ltab(%pc,%d0.l),%d0
	jmp	.Ltab(%pc,%d0.w)
.Ltab:	.word	0x0190, 0x0190, 0x01d0, 0x01d0
	.globl	mid
mid:	.fill	(0x190 - 8) / 2, 2, 0x4e71
.Lc0:	moveq	#0,%d0
	rts
	.fill	(0x1d0 - 0x194) / 2, 2, 0x4e71
.Lc1:	move.l	12(%sp),%d1
	bset	%d1,(%a0)
.Ldef:	rts
