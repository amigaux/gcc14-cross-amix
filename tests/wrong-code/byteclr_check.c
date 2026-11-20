#include "rt.h"

void clr_byte (unsigned char *, int);
void clr_byte_u8 (unsigned char *, unsigned char);
void set_byte (unsigned char *, int);
void flip_byte (unsigned char *, int);
void clr_bitmap (unsigned char *, int);

/* The byte is m[4]; the faulty BFCLR at offset 7 - n reaches back to m[1]
   for n = 30, so m[0..3] and m[5..7] are there to show any stray write.  */
static unsigned char m[8];

static void
fill (unsigned v)
{
  int i;
  for (i = 0; i < 8; i++)
    m[i] = v;
}

/* Every byte is v except m[4], which is want.  */
static int
only (unsigned v, unsigned want)
{
  int i;
  for (i = 0; i < 8; i++)
    if (m[i] != (i == 4 ? want : v))
      return 0;
  return 1;
}

int
byteclr_check (void)
{
  static const int big[] = { 8, 10, 15, 30 };	/* 1 << 31 overflows an int */
  int n, i;

  for (n = 0; n < 8; n++)
    {
      fill (0xff);
      clr_byte (&m[4], n);
      CHECK (only (0xff, 0xff & ~(1u << n)));
      fill (0xff);
      clr_byte_u8 (&m[4], n);
      CHECK (only (0xff, 0xff & ~(1u << n)));
      fill (0);
      set_byte (&m[4], n);
      CHECK (only (0, 1u << n));
      fill (0x5a);
      flip_byte (&m[4], n);
      CHECK (only (0x5a, 0x5a ^ (1u << n)));
    }
  for (i = 0; i < 4; i++)
    {
      n = big[i];
      fill (0xff);
      clr_byte (&m[4], n);		/* ~(1 << n) has a 0xff low byte */
      CHECK (only (0xff, 0xff));
      fill (0xff);
      clr_byte_u8 (&m[4], n);
      CHECK (only (0xff, 0xff));
      fill (0);
      set_byte (&m[4], n);		/* (unsigned char) (1 << n) is 0 */
      CHECK (only (0, 0));
      fill (0x5a);
      flip_byte (&m[4], n);
      CHECK (only (0x5a, 0x5a));
    }
  /* A bit array: m[n >> 3] loses bit n & 7 and nothing else changes.  */
  for (n = 0; n < 64; n++)
    {
      fill (0xff);
      clr_bitmap (m, n);
      for (i = 0; i < 8; i++)
	CHECK (m[i] == (i == n >> 3 ? 0xff & ~(1u << (n & 7)) : 0xff));
    }
  return failures;
}
