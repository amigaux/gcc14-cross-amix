/* bitops.c -- single-bit set and clear in memory.  m68k bset/bclr on a
   memory byte use only the low three bits of a register bit number; GCC
   modelled the full number until PR target/126121 (GCC master, July 2026),
   so a bit number of 8 or more could set the wrong bit.  C defines the
   result: 1 << 10 truncated to char is 0, and nothing changes.  */
#include "rt.h"

NOIPA void
setbit_char (unsigned char *a, int b)
{
  unsigned char c = 1 << b;
  *a |= c;
}

NOIPA void
clrbit_char (unsigned char *a, int b)
{
  *a &= ~(1 << (b & 7));
}

NOIPA void
setbit_word (u32 *w, int b)
{
  *w |= 1u << b;
}

NOIPA int
testbit (const unsigned char *a, int b)
{
  return (*a >> b) & 1;
}
