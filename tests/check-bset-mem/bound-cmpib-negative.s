| The bound "cmpi.b #-4" after "subq.b": compared unsigned at byte size it
| is 252, so 253 entries (vim's regrepeat has #-61, 196 entries).
| expect: rc=1 findings=1
	.text
	.globl	sw
sw:	move.l	4(%sp),%d0
	move.l	8(%sp),%a0
	subq.b	#5,%d0
	cmpi.b	#-4,%d0
	bhi	.Ldef
	and.l	#255,%d0
	add.l	%d0,%d0
	move.w	.Ltab(%pc,%d0.l),%d0
	jmp	.Ltab(%pc,%d0.w)
.Ltab:	.rept	253
	.word	0x03d0			| bset %d1,%a0@ as an instruction
	.endr
	.fill	(0x3d0 - 506) / 2, 2, 0x4e71
.Lc:	move.l	12(%sp),%d1		| at .Ltab + 0x3d0
	bset	%d1,(%a0)
.Ldef:	rts
