| Bit numbers bounded by a byte-sized mask (andi.b #7, as in Xrtg's
| ProcessPointerEvent, with and without a following andi.l #255) and by a
| 3-bit bfextu are shown safe; andi.b #15 is not a bound below 8.
| expect: rc=1 findings=1 functions=andib15
	.text
	.globl	andib7, andib7l, andib15, bfextu3
andib7:	move.l	4(%sp),%a0
	move.b	9(%sp),%d0
	andi.b	#7,%d0
	bclr	%d0,(%a0)
	rts
andib7l: move.l	4(%sp),%a0
	move.b	9(%sp),%d0
	andi.b	#7,%d0
	andi.l	#255,%d0
	bclr	%d0,89(%a0)
	rts
andib15: move.l	4(%sp),%a0
	move.b	9(%sp),%d0
	andi.b	#15,%d0
	bclr	%d0,(%a0)
	rts
bfextu3: move.l	4(%sp),%a0
	move.l	8(%sp),%a1
	bfextu	(%a0){#0:#3},%d0
	bset	%d0,(%a1)
	rts
