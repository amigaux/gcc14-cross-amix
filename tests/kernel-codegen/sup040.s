| sup040.s -- 68040 supervisor, cache and MMU instructions, assembled with -m68040.
	.text
	.globl	sup040
sup040:
	movec	%tc,%d0
	movec	%d0,%tc
	movec	%urp,%a0
	movec	%a0,%srp
	movec	%itt0,%d0
	movec	%d0,%itt1
	movec	%dtt0,%d0
	movec	%d0,%dtt1
	movec	%mmusr,%d0
	movec	%cacr,%d0
	movec	%vbr,%a1
	cinva	%bc
	cinvl	%dc,(%a0)
	cinvp	%ic,(%a0)
	cpusha	%bc
	cpushl	%dc,(%a0)
	cpushp	%bc,(%a0)
	pflush	(%a0)
	pflushn	(%a0)
	pflusha
	pflushan
	ptestr	(%a0)
	ptestw	(%a0)
	move16	(%a0)+,(%a1)+
	move16	(%a0)+,0x1000
	moves.l	(%a0),%d0
	rte
