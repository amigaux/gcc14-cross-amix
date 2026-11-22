| pic-pr126121.s without -fPIC: the table is read pc-relative and no .L
| label reaches the symbol table.  The same three sites must be flagged.
| expect: rc=1 findings=3 functions=f1,f2
	.text
	.globl	f1, f2
f1:	move.l	4(%sp),%d0
	move.l	8(%sp),%a0
	moveq	#3,%d1
	cmp.l	%d1,%d0
	bhi	.Ldef
	add.l	%d0,%d0
	move.w	.Ltab(%pc,%d0.l),%d0
	jmp	.Ltab(%pc,%d0.w)
.Ltab:	.word	0x0190, 0x0190, 0x01d0, 0x01d0
	.fill	(0x190 - 8) / 2, 2, 0x4e71
.Lc0:	move.l	12(%sp),%d1		| at .Ltab + 0x190
	bset	%d1,(%a0)		| flagged: %d1 not shown below 8
	rts
	.fill	(0x1d0 - 0x198) / 2, 2, 0x4e71
.Lc1:	move.l	12(%sp),%d1		| at .Ltab + 0x1d0
	moveq	#7,%d0
	sub.l	%d1,%d0
	bfclr	(%a0){%d0:#1}		| flagged: offset 7 - n
.Ldef:	moveq	#2,%d1
f2:	bset	%d1,(%a0)		| flagged: nothing is known at a function
	rts
