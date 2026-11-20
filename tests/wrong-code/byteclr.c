/* byteclr.c -- one bit of a byte in memory cleared, set or flipped with a
   variable bit number.  C defines the result for bit numbers 0 to 30:
   bits 8 and up are outside the byte, so the byte is left alone.  With the
   PR target/126121 fix alone, GCC turns the clear into BFCLR at offset
   7 - n, which for n >= 8 clears a bit in the byte BEFORE *a; the memory
   bit-field guard keeps it from doing so.  */
#include "rt.h"

NOIPA void
clr_byte (unsigned char *a, int n)
{
  *a &= ~(1 << n);
}

NOIPA void
clr_byte_u8 (unsigned char *a, unsigned char n)
{
  *a &= ~(1 << n);
}

NOIPA void
set_byte (unsigned char *a, int n)
{
  *a |= (unsigned char) (1 << n);
}

NOIPA void
flip_byte (unsigned char *a, int n)
{
  *a ^= (unsigned char) (1 << n);
}

/* A bit array: the bit number is masked, so every n is in range.  */
NOIPA void
clr_bitmap (unsigned char *m, int n)
{
  m[n >> 3] &= ~(1 << (n & 7));
}
