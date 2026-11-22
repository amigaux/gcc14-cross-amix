/* libc-diff.c -- libamixrt's _fpsetround, _fpsetsticky and siglongjmp
 * against libc's own members, under qemu-m68k.
 *
 * tools/test-amixrt.sh links this with libc's fpsetrnd.o, fpsetsticky.o and
 * siglongjmp.o from the sysroot, their functions renamed libc_*, and with
 * compat/amixrt_fpset.c and compat/amixrt_siglongjmp.c, renamed ours_*, and
 * runs it on qemu's 68040 and 68060 with tests/wrong-code/crt0.s, which
 * makes Linux system calls: none of these functions makes one.  Each case
 * loads the floating-point registers with the same value before each of the
 * two calls and compares what the two return and leave behind.  setcontext
 * is this file's: it records the context it is given, which siglongjmp
 * would resume, and returns.
 */
typedef unsigned long u32;

int sys_write(int fd, const void *buf, unsigned len);
int libc_fpsetround(int), ours_fpsetround(int);
int libc_fpsetsticky(int), ours_fpsetsticky(int);
void libc_siglongjmp(int *, int), ours_siglongjmp(int *, int);

int __flt_rounds;
static int failures, cases;

static void put(const char *s)
{
	unsigned n = 0;

	while (s[n]) n++;
	sys_write(1, s, n);
}

static void puthex(u32 v)
{
	char b[9];
	int i;

	for (i = 7; i >= 0; i--, v >>= 4) b[i] = "0123456789abcdef"[v & 15];
	b[8] = 0;
	put(b);
}

static void differ(const char *what, u32 arg, u32 start, u32 l, u32 o)
{
	failures++;
	put("FAIL ");
	put(what);
	put(" arg ");
	puthex(arg);
	put(" from ");
	puthex(start);
	put(": libc ");
	puthex(l);
	put(", ours ");
	puthex(o);
	put("\n");
}

static u32 get_fpcr(void) { u32 v; __asm__ volatile ("fmovel %%fpcr, %0" : "=d" (v)); return v; }
static void set_fpcr(u32 v) { __asm__ volatile ("fmovel %0, %%fpcr" : : "d" (v)); }
static u32 get_fpsr(void) { u32 v; __asm__ volatile ("fmovel %%fpsr, %0" : "=d" (v)); return v; }
static void set_fpsr(u32 v) { __asm__ volatile ("fmovel %0, %%fpsr" : : "d" (v)); }

static void fp_cases(void)
{
	static const u32 fpcrs[] = { 0, 0x10, 0x20, 0x30, 0x40, 0x80, 0xc0, 0xff00, 0x3c50, 0xfff0 };
	static const u32 fpsrs[] = { 0, 0x08, 0xf8, 0xff, 0x0f000000, 0x0800ff00, 0x0fffff00 };
	static const int args[] = { 0, 1, 2, 3, 4, 7, -1, 0x55, 0x100, 0x1f8 };
	unsigned i, j;

	for (i = 0; i < sizeof fpcrs / sizeof fpcrs[0]; i++)
		for (j = 0; j < sizeof args / sizeof args[0]; j++) {
			u32 rl, ro, cl, co;
			int fl, fo;

			cases++;
			__flt_rounds = 99;
			set_fpcr(fpcrs[i]);
			rl = libc_fpsetround(args[j]);
			cl = get_fpcr();
			fl = __flt_rounds;
			__flt_rounds = 99;
			set_fpcr(fpcrs[i]);
			ro = ours_fpsetround(args[j]);
			co = get_fpcr();
			fo = __flt_rounds;
			if (rl != ro) differ("_fpsetround's result", args[j], fpcrs[i], rl, ro);
			if (cl != co) differ("_fpsetround's FPCR", args[j], fpcrs[i], cl, co);
			if (fl != fo) differ("_fpsetround's __flt_rounds", args[j], fpcrs[i], fl, fo);
		}
	set_fpcr(0);
	for (i = 0; i < sizeof fpsrs / sizeof fpsrs[0]; i++)
		for (j = 0; j < sizeof args / sizeof args[0]; j++) {
			u32 rl, ro, sl, so;

			cases++;
			set_fpsr(fpsrs[i]);
			rl = libc_fpsetsticky(args[j]);
			sl = get_fpsr();
			set_fpsr(fpsrs[i]);
			ro = ours_fpsetsticky(args[j]);
			so = get_fpsr();
			if (rl != ro) differ("_fpsetsticky's result", args[j], fpsrs[i], rl, ro);
			if (sl != so) differ("_fpsetsticky's FPSR", args[j], fpsrs[i], sl, so);
		}
	set_fpsr(0);
}

/* siglongjmp: the buffer setcontext is given, byte for byte. */
static int *seen;
static int seen_buf[128];

int setcontext(int *uc)
{
	int i;

	seen = uc;
	for (i = 0; i < 128; i++) seen_buf[i] = uc[i];
	return -1;
}

static void fill(int *b, int seed)
{
	int i;

	for (i = 0; i < 128; i++) b[i] = seed * 1000 + i;
}

static void jmp_cases(void)
{
	static const int vals[] = { 0, 1, 2, 42, -1, 0x7fffffff };
	static int bl[128], bo[128];
	int l[128], i;
	unsigned j;

	for (j = 0; j < sizeof vals / sizeof vals[0]; j++) {
		cases++;
		fill(bl, j);
		fill(bo, j);
		libc_siglongjmp(bl, vals[j]);
		if (seen != bl) differ("libc siglongjmp's setcontext argument", vals[j], 0, (u32)seen, (u32)bl);
		for (i = 0; i < 128; i++) l[i] = seen_buf[i];
		ours_siglongjmp(bo, vals[j]);
		if (seen != bo) differ("our siglongjmp's setcontext argument", vals[j], 0, (u32)seen, (u32)bo);
		for (i = 0; i < 128; i++)
			if (l[i] != seen_buf[i]) differ("siglongjmp's context word", vals[j], i, l[i], seen_buf[i]);
	}
}

int main(void)
{
	fp_cases();
	jmp_cases();
	if (failures == 0) put("all cases agree with libc\n");
	return failures != 0;
}
