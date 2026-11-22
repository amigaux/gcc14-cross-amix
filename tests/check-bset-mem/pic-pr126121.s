| The PR 126121 patterns in -fPIC code, with .L labels kept in the symbol
| table (the switch table's base and a label loaded through the GOT): an
| unmasked bset %d1,(%a0) in a case, and bfclr at offset 7 - n after a .L
| label, both flagged in f1, not in a function named after the label.  The
| table words are skipped.  A function label is still a boundary: %d1 is
| known small at the end of f1, and f2's bset must be flagged all the same.
| nonpic-pr126121.s is the same code without the kept labels.
| expect: rc=1 findings=3 functions=f1,f2
	.text
	.globl	f1, f2
f1:	move.l	4(%sp),%d0
	move.l	8(%sp),%a0
	moveq	#3,%d1
	cmp.l	%d1,%d0
	bhi	.Ldef
	add.l	%d0,%d0
	move.l	.Ltab@GOT(%a5),%a1	| keeps .Ltab as a symbol
	move.w	(0,%a1,%d0.l),%d0
	jmp	.Ltab(%pc,%d0.w)
.Ltab:	.word	0x0190, 0x0190, 0x01d0, 0x01d0
	.fill	(0x190 - 8) / 2, 2, 0x4e71
.Lc0:	move.l	12(%sp),%d1		| at .Ltab + 0x190
	bset	%d1,(%a0)		| flagged: %d1 not shown below 8
	rts
	.fill	(0x1d0 - 0x198) / 2, 2, 0x4e71
.Lc1:	move.l	12(%sp),%d1		| at .Ltab + 0x1d0
	move.l	.Lj@GOT(%a5),%a1	| keeps .Lj as a symbol
.Lj:	moveq	#7,%d0
	sub.l	%d1,%d0
	bfclr	(%a0){%d0:#1}		| flagged: offset 7 - n
.Ldef:	moveq	#2,%d1
f2:	bset	%d1,(%a0)		| flagged: nothing is known at a function
	rts
