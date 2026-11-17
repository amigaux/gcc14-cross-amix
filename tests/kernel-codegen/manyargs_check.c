/* manyargs_check.c -- the callers, and the same hash computed here. */
#include "rt.h"

u32 many (char, short, int, s64, void *, unsigned char, unsigned short, u64,
	  char, int, short, const char *);
s64 vsum (int, ...);

static u32
expect (char c, short s, int i, s64 ll, void *p, unsigned char uc,
	unsigned short us, u64 ull, char c2, int i2, short s2, const char *str)
{
  u32 h = 0;
  h = h * 31 + (u32) c;
  h = h * 31 + (u32) s;
  h = h * 31 + (u32) i;
  h = h * 31 + (u32) (ll >> 32) + (u32) ll;
  h = h * 31 + (u32) (unsigned long) p;
  h = h * 31 + uc;
  h = h * 31 + us;
  h = h * 31 + (u32) (ull >> 32) + (u32) ull;
  h = h * 31 + (u32) c2;
  h = h * 31 + (u32) i2;
  h = h * 31 + (u32) s2;
  h = h * 31 + (u32) str[0];
  return h;
}

int
manyargs_check (void)
{
  static int obj;
  CHECK (many (-3, -1234, 0x7fffffff, -0x123456789LL, &obj, 0xfe, 0xfedc,
	       0xfedcba9876543210ULL, 'x', -77, 32000, "hello")
	 == expect (-3, -1234, 0x7fffffff, -0x123456789LL, &obj, 0xfe, 0xfedc,
		    0xfedcba9876543210ULL, 'x', -77, 32000, "hello"));
  CHECK (vsum (4, 4, 10, 8, (s64) 1 << 40, 4, -3, 8, (s64) -5) == 10 + ((s64) 1 << 40) - 3 - 5);
  return failures;
}
