/* rt.c -- output, a pass/fail count and the two memory functions a
   freestanding build may call. */
#include "rt.h"

int failures;

void
put (const char *s)
{
  unsigned n = 0;
  while (s[n])
    n++;
  sys_write (1, s, n);
}

/* Not turned back into calls to themselves by loop distribution.  */
#define NOLDP __attribute__((optimize ("no-tree-loop-distribute-patterns")))

NOLDP void *
memcpy (void *d, const void *s, unsigned long n)
{
  char *dp = d;
  const char *sp = s;
  while (n--)
    *dp++ = *sp++;
  return d;
}

NOLDP void *
memset (void *d, int c, unsigned long n)
{
  char *dp = d;
  while (n--)
    *dp++ = c;
  return d;
}

static void
putdec (unsigned v)
{
  char b[11];
  int i = 10;
  b[i] = 0;
  do
    b[--i] = '0' + v % 10;
  while (v /= 10);
  put (b + i);
}

void
check (int ok, const char *what, int line)
{
  if (ok)
    return;
  failures++;
  put ("FAIL line ");
  putdec (line);
  put (": ");
  put (what);
  put ("\n");
}

static void
run (const char *name, int (*fn) (void))
{
  int before = failures;
  fn ();
  put (name);
  put (failures == before ? ": ok\n" : ": FAILED\n");
}

int
main (void)
{
  run ("byteclr", byteclr_check);
  run ("fmo", fmo_check);
  put (failures ? "FAILED\n" : "all cases passed\n");
  return failures != 0;
}
