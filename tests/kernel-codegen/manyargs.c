/* manyargs.c -- twelve arguments of mixed width, all on the stack: the
   offsets of each, and the promotion of char and short, have to agree.  */
#include "rt.h"
#include <stdarg.h>

NOIPA u32
many (char c, short s, int i, s64 ll, void *p, unsigned char uc,
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

NOIPA s64
vsum (int n, ...)
{
  va_list ap;
  s64 t = 0;
  va_start (ap, n);
  while (n-- > 0)
    {
      int kind = va_arg (ap, int);
      if (kind == 8)
	t += va_arg (ap, s64);
      else
	t += va_arg (ap, int);
    }
  va_end (ap);
  return t;
}
