| sup030.s -- 68030 supervisor and MMU instructions, assembled with -m68030.
	.text
	.globl	sup030
sup030:
	move.w	%sr,%d0
	ori.w	#0x0700,%sr
	andi.w	#0xf8ff,%sr
	move.w	%d0,%sr
	movec	%vbr,%a0
	movec	%a0,%vbr
	movec	%cacr,%d0
	movec	%d0,%cacr
	movec	%caar,%d1
	movec	%sfc,%d1
	movec	%d1,%dfc
	movec	%msp,%a1
	movec	%isp,%a1
	moves.l	(%a0),%d0
	moves.l	%d0,(%a0)
	pmove	%tc,(%a0)
	pmove	(%a0),%tc
	pmove	%crp,(%a0)
	pmove	(%a0),%srp
	pmove	%tt0,(%a0)
	pmove	(%a0),%tt1
	pmove	%psr,%d0
	pmovefd	(%a0),%tc
	pflusha
	pflush	#0,#0
	pflush	#1,#1,(%a0)
	ptestr	#1,(%a0),#7
	ptestw	#5,(%a1),#7,%a0
	ploadr	#1,(%a0)
	ploadw	#5,(%a0)
	rte
	stop	#0x2700
	reset
