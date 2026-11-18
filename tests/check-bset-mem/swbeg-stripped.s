| The swbeg case linked and stripped: with no symbols objdump prints the
| table base as "%pc@(0x...,%d0:w)" and branch targets as bare 0x addresses
| (the k30 kernel).  The table must still be found.
| link: strip
| expect: rc=1 findings=1
	.text
	.globl	_start
_start:	move.l	4(%sp),%d0
	move.l	8(%sp),%a0
	moveq	#3,%d1
	cmp.l	%d1,%d0
	bhi	.Ldef
	add.l	%d0,%d0
	move.w	.Ltab(%pc,%d0.l),%d0
	jmp	.Ltab(%pc,%d0.w)
	.word	0x4afc, 4
.Ltab:	.word	0x0190, 0x0190, 0x01d0, 0x01d0
	.fill	(0x190 - 8) / 2, 2, 0x4e71
.Lc0:	moveq	#0,%d0			| at .Ltab + 0x190
	rts
	.fill	(0x1d0 - 0x194) / 2, 2, 0x4e71
.Lc1:	move.l	12(%sp),%d1		| at .Ltab + 0x1d0
	bset	%d1,(%a0)
.Ldef:	rts
