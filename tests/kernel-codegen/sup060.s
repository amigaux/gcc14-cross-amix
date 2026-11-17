| sup060.s -- 68060 supervisor, cache and MMU instructions, assembled with -m68060.
	.text
	.globl	sup060
sup060:
	movec	%pcr,%d0
	movec	%d0,%pcr
	movec	%buscr,%d0
	movec	%d0,%buscr
	movec	%tc,%d0
	movec	%urp,%a0
	movec	%srp,%a0
	movec	%itt0,%d0
	movec	%dtt1,%d0
	movec	%cacr,%d0
	movec	%d0,%cacr
	plpar	(%a0)
	plpaw	(%a1)
	cinva	%bc
	cinvl	%ic,(%a0)
	cpusha	%dc
	cpushp	%bc,(%a0)
	pflush	(%a0)
	pflushan
	lpstop	#0x2700
	moves.l	(%a0),%d0
	rte
