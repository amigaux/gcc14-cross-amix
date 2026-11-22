/* amixrt_fpset.c -- _fpsetround and _fpsetsticky, for libamixrt.so.
 *
 * libc.so.1 exports neither; AMIX has them only as static members of
 * usr/ccs/lib/libc.so (fpsetrnd.o, fpsetsticky.o), with fpsetround and
 * fpsetsticky as weak names for them.  libm.a's sqrt calls them, so a shared
 * library that links sqrt from libm.a (CDE's DtSearch) needs them.  They work
 * on the 68881/68882 control and status registers, which every AMIX machine
 * has, or emulates, and behave as libc's do, read off its code:
 *
 * _fpsetround(rnd) puts rnd & 3 in the FPCR's rounding field, bits 4-5, which
 * <ieeefp.h> numbers in the 68881's order (FP_RN 0, FP_RZ 1, FP_RM 2, FP_RP
 * 3), and records the mode in libc.so.1's __flt_rounds the way libc does:
 * FP_RN 0, FP_RZ 1, FP_RP 2, FP_RM 3.  It returns the old FPCR with the
 * rounding field and the top byte cleared, shifted right by 4 -- so not the
 * previous mode <ieeefp.h> promises, but the precision and enable bits; with
 * the usual FPCR of 0 that is 0, FP_RN.  libm's sqrt restores the mode with
 * what it returned, so the same value has to come back here as from libc.
 *
 * _fpsetsticky(sticky) replaces the FPSR's accrued-exception byte, bits 0-7,
 * with the low byte of sticky (FP_X_* in <ieeefp.h> are those bits), and
 * returns the old FPSR with that byte cleared, again as libc does, rather
 * than the previous flags.
 */
extern int __flt_rounds;

int _fpsetround(int rnd)
{
	unsigned long fpcr;
	int r = rnd & 3;

	__flt_rounds = r == 1 ? 1 : r == 3 ? 2 : r == 2 ? 3 : 0;
	__asm__ volatile ("fmovel %%fpcr, %0" : "=d" (fpcr));
	fpcr &= 0x00ffffcfUL;
	__asm__ volatile ("fmovel %0, %%fpcr" : : "d" (fpcr | ((unsigned long)r << 4)));
	return (int)(fpcr >> 4);
}

int _fpsetsticky(int sticky)
{
	unsigned long fpsr;

	__asm__ volatile ("fmovel %%fpsr, %0" : "=d" (fpsr));
	fpsr &= 0xffffff00UL;
	__asm__ volatile ("fmovel %0, %%fpsr" : : "d" (fpsr | ((unsigned long)sticky & 0xff)));
	return (int)fpsr;
}

int fpsetround(int) __attribute__((weak, alias("_fpsetround")));
int fpsetsticky(int) __attribute__((weak, alias("_fpsetsticky")));
