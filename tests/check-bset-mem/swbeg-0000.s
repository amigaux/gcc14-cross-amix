| A switch table after a 4-byte count prefix "0000 N", as
| GCC 2.7.2.3 makes it.  The entries decode as
| bclr/bset %d0,%a0@ and must be skipped as data; the one real bset with an
| unknown bit number in a case must still be flagged.
| expect: rc=1 findings=1
	.text
	.globl	sw
sw:	move.l	4(%sp),%d0
	move.l	8(%sp),%a0
	moveq	#3,%d1
	cmp.l	%d1,%d0
	bhi	.Ldef
	add.l	%d0,%d0
	move.w	.Ltab(%pc,%d0.l),%d0
	jmp	.Ltab(%pc,%d0.w)
	.word	0x0000, 4
.Ltab:	.word	0x0190, 0x0190, 0x01d0, 0x01d0
	.fill	(0x190 - 8) / 2, 2, 0x4e71
.Lc0:	moveq	#0,%d0			| at .Ltab + 0x190
	rts
	.fill	(0x1d0 - 0x194) / 2, 2, 0x4e71
.Lc1:	move.l	12(%sp),%d1		| at .Ltab + 0x1d0
	bset	%d1,(%a0)
.Ldef:	rts
