#include "rt.h"

void setbit_char (unsigned char *, int);
void clrbit_char (unsigned char *, int);
void setbit_word (u32 *, int);
int testbit (const unsigned char *, int);

int
bitops_check (void)
{
  unsigned char a = 0;
  u32 w = 0;
  setbit_char (&a, 1);
  CHECK (a == 2);
  setbit_char (&a, 10);		/* (char) (1 << 10) == 0 */
  CHECK (a == 2);
  setbit_char (&a, 7);
  CHECK (a == 0x82);
  clrbit_char (&a, 7);
  CHECK (a == 2);
  setbit_word (&w, 20);
  CHECK (w == 1u << 20);
  CHECK (testbit (&a, 1) == 1 && testbit (&a, 2) == 0);
  return failures;
}
