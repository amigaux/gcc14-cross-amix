/* cxxrt-host.c -- compat/amix_cxxrt.c's floor and frexpl against the host's,
 * run by tools/test-compat-host.sh.  That script builds amix_cxxrt.c with
 * AMIX_CXXRT_HOST_TEST and the two functions renamed, so this program calls
 * ours as amix_test_floor and amix_test_frexpl and the host C library's
 * under their own names.  Both are IEEE double on both sides; only the byte
 * order differs, and the code does not depend on it.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double amix_test_floor(double);
long double amix_test_frexpl(long double, int *);

static int failed, checked;

/* Equal bit for bit, so the sign of zero counts; any NaN matches any NaN. */
static void check(double x)
{
	double want = floor(x), got = amix_test_floor(x);

	checked++;
	if (isnan(want) && isnan(got))
		return;
	if (memcmp(&want, &got, sizeof want) != 0) {
		printf("  FAIL  floor(%.17g): %.17g, want %.17g\n", x, got, want);
		failed = 1;
	}
}

int main(void)
{
	static const double edge[] = {
		0.0, -0.0, 0.5, -0.5, 1.0, -1.0, 1.5, -1.5, 2.0, -2.0,
		0.9999999999999999, -0.9999999999999999, 1e-300, -1e-300,
		4.9e-324, -4.9e-324, 2.2250738585072014e-308, -2.2250738585072014e-308,
		1048575.5, -1048575.5, 1048576.5, -1048576.5, 2097151.75, -2097151.75,
		4503599627370495.5, -4503599627370495.5, 4503599627370496.0,
		-4503599627370496.0, 9007199254740993.0, -9007199254740993.0,
		1e300, -1e300, 123456.789, -123456.789,
	};
	unsigned i;
	int e1, e2;

	for (i = 0; i < sizeof edge / sizeof edge[0]; i++)
		check(edge[i]);
	check(HUGE_VAL);
	check(-HUGE_VAL);
	check(nan(""));
	/* Every exponent, with random fractions and both signs. */
	srand(1);
	for (i = 0; i < 200000; i++) {
		unsigned long long u = ((unsigned long long) rand() << 33)
			^ ((unsigned long long) rand() << 11) ^ (unsigned long long) rand();
		double x;

		memcpy(&x, &u, sizeof x);
		check(x);
		check(ldexp(x - floor(x), (int) (i % 70) - 10));
	}
	/* frexpl is frexp of the value as a double, as libstdc++'s stub is. */
	for (i = 0; i < sizeof edge / sizeof edge[0]; i++) {
		double m1 = frexp(edge[i], &e1);
		long double m2 = amix_test_frexpl(edge[i], &e2);

		checked++;
		if ((long double) m1 != m2 || e1 != e2) {
			printf("  FAIL  frexpl(%.17g)\n", edge[i]);
			failed = 1;
		}
	}
	printf("cxxrt floor and frexpl: %d values, %s\n", checked, failed ? "FAIL" : "all as the host's");
	return failed;
}
