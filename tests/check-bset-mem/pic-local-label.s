| A switch in -fPIC code, as GCC 14 makes it in libstdc++: the table's
| address is loaded through the GOT, so the assembler keeps its .L label in
| the symbol table, and objdump prints that label right after the jmp.  It
| must not be taken for a new function: that ended the table, whose entries
| decode as bclr/bset %d0,%a0@, and they were flagged (six false findings
| in a PIC libstdc++).  The one bset in a case has its bit number masked
| with & 7, so nothing here is flagged.
| expect: rc=0 findings=0
	.text
	.globl	sw
sw:	move.l	4(%sp),%d0
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
.Lc0:	moveq	#0,%d0			| at .Ltab + 0x190
	rts
	.fill	(0x1d0 - 0x194) / 2, 2, 0x4e71
.Lc1:	move.l	12(%sp),%d1		| at .Ltab + 0x1d0
	moveq	#7,%d0
	and.l	%d0,%d1
	bset	%d1,(%a0)
.Ldef:	rts
