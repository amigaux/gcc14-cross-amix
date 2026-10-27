/* xpage.c -- misaligned accesses across a page boundary, for the 68040.
 *
 * The compiler's default is -mno-strict-align (docs/TOOLCHAIN.md,
 * "-mno-strict-align is the default"), so its code can make a long or word
 * access at an address that is not a multiple of its size.  Every CPU AMIX runs
 * on does that in hardware, but on a 68040 the case where the access crosses
 * into a page that is not resident needs the kernel's help: the 68040 reports
 * the fault at the access's START address, on the page that is already there.
 * A kernel that believes it faults that page in again, forever, and the machine
 * wedges -- 040/060 port ISSUE-37, fixed in port kernels from 68040-260728-26
 * on.  This probe makes exactly those accesses and checks what they read and
 * wrote.
 *
 * Every step gets a page pair of its own from /dev/zero, so the second page is
 * demand-zero and not resident when the access reaches it -- the fault is
 * taken in the middle of the access, which is the whole point.  The accesses
 * are inline asm, so they are one instruction whatever the flags; the last step
 * is compiled code, a 4-byte memcpy, which under the default is one move.l
 * (check: objdump -d probe/xpage | grep -A4 '<copy_in>:').
 *
 * ON A 68040 KERNEL OLDER THAN 68040-260728-26 THIS WEDGES THE MACHINE.  The
 * probe reads the kernel tag from uname and refuses to run on one; pass
 * --anyway to override, and expect to reset the machine.  Each line is flushed
 * before its step runs, so if it does hang, the last line names the access.
 *
 *   tools/amix-cc.sh -O2 -o probe/xpage probe/xpage.c
 *   tools/push-run.sh probe/xpage */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/utsname.h>

/* <sys/mman.h> declares mmap but not munmap. */
extern int munmap(caddr_t, size_t);

static long page;
static int zfd;
static int failed;

/* Two pages, neither touched yet. */
static unsigned char *fresh(void)
{
	caddr_t p = mmap((caddr_t)0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE, zfd, 0);

	if (p == (caddr_t)-1) {
		perror("mmap /dev/zero");
		exit(2);
	}
	return (unsigned char *)p;
}

static void done_with(unsigned char *p)
{
	munmap((caddr_t)p, 2 * page);
}

static unsigned long read_long(const void *p)
{
	unsigned long v;

	__asm__ volatile ("move.l (%1),%0" : "=d" (v) : "a" (p) : "memory");
	return v;
}

static unsigned short read_word(const void *p)
{
	unsigned short v;

	__asm__ volatile ("move.w (%1),%0" : "=d" (v) : "a" (p) : "memory");
	return v;
}

static void write_long(void *p, unsigned long v)
{
	__asm__ volatile ("move.l %1,(%0)" : : "a" (p), "d" (v) : "memory");
}

static void add_long(void *p, unsigned long v)
{
	__asm__ volatile ("add.l %1,(%0)" : : "a" (p), "d" (v) : "memory");
}

/* What the compiler itself emits for an unaligned 4-byte load. */
__attribute__((noinline)) static unsigned long copy_in(const void *p)
{
	unsigned long v;

	memcpy(&v, p, 4);
	return v;
}

static void step(const char *what)
{
	printf("%-46s", what);
	fflush(stdout);
}

static void check(unsigned long got, unsigned long want)
{
	if (got == want)
		printf("ok\n");
	else {
		printf("WRONG: got %08lx, want %08lx\n", got, want);
		failed++;
	}
	fflush(stdout);
}

/* The four bytes at p, read one at a time: never misaligned. */
static unsigned long bytes_at(const unsigned char *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
	       (unsigned long)p[2] << 8 | p[3];
}

/* The port names its kernels <cpu>-<yymmdd>-<nn>, and puts the tag at the end
 * of the machine field: "Amiga (Unlimited) 68040-260728-26".  Returns 1 if this
 * is a 68040 kernel older than the ISSUE-37 fix. */
static int old_040_kernel(const char *machine)
{
	const char *t = strstr(machine, "68040-");
	long date, seq;

	if (t == NULL || sscanf(t + 6, "%6ld-%ld", &date, &seq) != 2)
		return 0;
	return date < 260728 || (date == 260728 && seq < 26);
}

int main(int argc, char **argv)
{
	struct utsname u;
	unsigned char *b, *end;

	uname(&u);
	page = sysconf(_SC_PAGESIZE);
	printf("kernel:  %s\npage:    %ld bytes\n", u.machine, page);
	fflush(stdout);
	if (old_040_kernel(u.machine) && !(argc > 1 && strcmp(argv[1], "--anyway") == 0)) {
		printf("refusing: a 68040 kernel older than 68040-260728-26 wedges on this\n"
		       "probe (ISSUE-37).  --anyway to run it regardless.\n");
		return 3;
	}
	if ((zfd = open("/dev/zero", O_RDWR)) < 0) {
		perror("/dev/zero");
		return 2;
	}

	/* Page 1 is touched before each access below, page 2 never: the access
	 * starts on a resident page and faults where it crosses. */
	step("long read at end-4, no crossing (control):");
	b = fresh(); end = b + page;
	end[-4] = 0x11; end[-3] = 0x22; end[-2] = 0x33; end[-1] = 0x44;
	check(read_long(end - 4), 0x11223344UL);
	done_with(b);

	step("long read at end-2:");
	b = fresh(); end = b + page;
	end[-2] = 0xab; end[-1] = 0xcd;
	check(read_long(end - 2), 0xabcd0000UL);
	done_with(b);

	step("long read at end-1:");
	b = fresh(); end = b + page;
	end[-1] = 0xab;
	check(read_long(end - 1), 0xab000000UL);
	done_with(b);

	step("long read at end-3:");
	b = fresh(); end = b + page;
	end[-3] = 0xab; end[-2] = 0xcd; end[-1] = 0xef;
	check(read_long(end - 3), 0xabcdef00UL);
	done_with(b);

	step("word read at end-1:");
	b = fresh(); end = b + page;
	end[-1] = 0xab;
	check(read_word(end - 1), 0xab00UL);
	done_with(b);

	step("long write at end-2:");
	b = fresh(); end = b + page;
	end[-3] = 0;
	write_long(end - 2, 0x11223344UL);
	check(bytes_at(end - 2), 0x11223344UL);
	done_with(b);

	step("long add (read-modify-write) at end-2:");
	b = fresh(); end = b + page;
	end[-1] = 0x01;
	add_long(end - 2, 0x00000005UL);
	check(bytes_at(end - 2), 0x00010005UL);
	done_with(b);

	/* Neither page resident: the first fault is on the start page, where the
	 * 68040's report is right, and the second is where it is not. */
	step("long read at end-2, neither page resident:");
	b = fresh(); end = b + page;
	check(read_long(end - 2), 0UL);
	done_with(b);

	step("compiled memcpy of 4 bytes at end-3:");
	b = fresh(); end = b + page;
	end[-3] = 0xab; end[-2] = 0xcd; end[-1] = 0xef;
	check(copy_in(end - 3), 0xabcdef00UL);
	done_with(b);

	printf("done, %d wrong\n", failed);
	return failed != 0;
}
