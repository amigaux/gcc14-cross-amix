/* retabi-period.c -- the gcc 2.7.2.3 half of probe/retabi.c.
 *
 * Compiled by AMIX's own compiler, so that every return value in the probe
 * crosses between the two compilers in both directions.  See retabi.c. */

struct pair { int q, r; };

extern char *g14_ptr(char *, int);
extern struct pair g14_pair(int, int);

char *old_ptr(char *p, int n)
{
	return p + n;
}

struct pair old_pair(int a, int b)
{
	struct pair r;

	r.q = a / b;
	r.r = a % b;
	return r;
}

/* The comparison is made after the call, so p + n is not what %a0 happens to
 * hold when the callee returns. */
int old_calls_ptr(char *p, int n)
{
	return g14_ptr(p, n) == p + n;
}

int old_calls_pair(int a, int b)
{
	struct pair r;

	r = g14_pair(a, b);
	return r.q == a / b && r.r == a % b;
}
