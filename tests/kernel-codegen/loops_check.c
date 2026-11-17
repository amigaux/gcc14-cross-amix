#include "rt.h"

u32 count_until (u32, u32);
u32 sum_n (const unsigned short *, u32);
void fill (u32 *, u32, unsigned short);
int count_sign (int);

int
loops_check (void)
{
  static unsigned short v[70000 / 2];
  static u32 f[300];
  unsigned i;
  for (i = 0; i < sizeof v / sizeof v[0]; i++)
    v[i] = i & 0xff;
  CHECK (count_until (0x80010005u, 100000) == 100000);
  CHECK (count_until (5, 100) == 5);
  CHECK (count_until (0x10000u, 0) == 0x10000u);
  CHECK (sum_n (v, 35000) == 35000u / 256 * (255 * 256 / 2) + (35000u % 256) * (35000u % 256 - 1) / 2);
  fill (f, 0x1234, 300);
  CHECK (f[0] == 0x1234 && f[299] == 0x1234);
  CHECK (count_sign (70000) == 70000);
  CHECK (count_sign (0) == 0);
  return failures;
}
