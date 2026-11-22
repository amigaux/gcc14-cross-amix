/* cxxso-cmain.c -- a C program that uses a C++ shared library, for
 * tools/test-cxx-shared.sh.  Linked with plain gcc and no option: it has no
 * C++ runtime of its own, and libstdc++.so.6 comes in because the library
 * NEEDs it.  Inside the library an exception is thrown in libcxxsoa.so.1 and
 * caught in libcxxsob.so.1.
 */
#include <stdio.h>

extern int b_c_api(int n);

int main(void)
{
	int small = b_c_api(5), big = b_c_api(11);
	int ok = small == 25 && big == -121;

	printf("cxxso-cmain: b_c_api(5)=%d (25) b_c_api(11)=%d (-121) -> %s\n",
	       small, big, ok ? "PASS" : "FAIL");
	fflush(stdout);
	return ok ? 0 : 1;
}
