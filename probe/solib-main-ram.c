/* solib-main-ram.c -- call one library function, then sleep.
 *
 * Linked by tools/test-amixrt.sh as ram-plain (against libsolibram.so.1)
 * and ram-amixrt (against libsolibramrt.so.1, which NEEDs libamixrt.so.1).
 * Started many times over, the two show in ps -l and in the kernel's
 * freemem what libamixrt.so.1 costs each process.  The argument is how many
 * seconds to sleep, 120 by default.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int ram_fmt(char *, int, int);

int main(int argc, char **argv)
{
	char buf[16];

	ram_fmt(buf, sizeof buf, 42);
	sleep(argc > 1 ? (unsigned)atoi(argv[1]) : 120);
	return buf[0] == '4' ? 0 : 1;
}
