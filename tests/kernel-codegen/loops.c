/* loops.c -- counted loops that the m68k back end can close with dbra.
   dbra counts a 16-bit word, so a 32-bit counter needs a fix-up after the
   borrow.  In output_dbcc_and_branch GCC 16.2.0 left that fix-up on jpl, so
   a count above 2^31 ended at the first borrow (AmigaPorts gcc commit
   f831bb9945, patch 0007).  These loops check the counts whichever pattern
   a compiler picks; with GCC 14.2 and 16.2 only sum_n gets a dbra, from the
   decrement_and_branch patterns, which were already right.  */
#include "rt.h"

NOIPA u32
count_until (u32 n, u32 stop)
{
  u32 cnt = 0;
  while (n--)
    if (++cnt == stop)
      break;
  return cnt;
}

NOIPA u32
sum_n (const unsigned short *p, u32 n)
{
  u32 s = 0;
  while (n--)
    s += *p++;
  return s;
}

NOIPA void
fill (u32 *p, u32 v, unsigned short n)
{
  while (n--)
    *p++ = v;
}

NOIPA int
count_sign (int n)
{
  /* A loop whose exit is a sign test (dbpl/dbmi in output_dbcc_and_branch). */
  int k = 0;
  while (--n >= 0)
    k++;
  return k;
}
