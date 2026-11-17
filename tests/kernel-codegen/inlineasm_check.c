#include "rt.h"

u32 asm_add (u32, u32);
u32 asm_swap (u32);
u32 asm_bfextu (u32, int, int);
void asm_store (u32 *, u32);
u32 asm_load_post (const u32 **);
int asm_cas (u32 *, u32, u32);
u32 asm_lea (u32 *, int);

int
inlineasm_check (void)
{
  static u32 arr[4] = { 11, 22, 33, 44 };
  const u32 *p = arr;
  u32 cell = 5;
  CHECK (asm_add (0xffff0001u, 0x0001ffffu) == 0x00010000u);
  CHECK (asm_swap (0x12345678u) == 0x56781234u);
  CHECK (asm_bfextu (0xf0f0abcdu, 16, 8) == 0xab);
  asm_store (&cell, 0xdeadbeef);
  CHECK (cell == 0xdeadbeef);
  CHECK (asm_load_post (&p) == 11 && p == arr + 1);
  CHECK (asm_cas (&cell, 0xdeadbeef, 7) == (int) 0xdeadbeef && cell == 7);
  CHECK (asm_cas (&cell, 1, 9) == 7 && cell == 7);
  CHECK (asm_lea (arr, 3) == 44);
  return failures;
}
