/* muldiv64_check.c -- results checked against constants the compiler folds
   at compile time, so the expected values never go through the code under
   test.  */
#include "rt.h"

u64 mul64 (u64, u64);
s64 smul64 (s64, s64);
u64 umul32x32 (u32, u32);
s64 smul32x32 (s32, s32);
u32 umulhi32 (u32, u32);
u64 udiv64 (u64, u64);
u64 umod64 (u64, u64);
s64 sdiv64 (s64, s64);
s64 smod64 (s64, s64);
u64 udiv64_32 (u64, u32);
u64 udiv64_by10 (u64);
u64 umod64_by10 (u64);
u64 udiv64_by1000 (u64);
s64 sdiv64_by7 (s64);
u64 udiv64_by_pow2 (u64);
u32 udiv32_by10 (u32);
u32 udiv32_by60 (u32);
s32 sdiv32_by3 (s32);
u32 umod32_by1000 (u32);
u32 udiv32 (u32, u32);
s32 smod32 (s32, s32);
u64 shl64 (u64, int);
u64 shr64 (u64, int);
s64 sar64 (s64, int);

#define A 0x123456789abcdef1ULL
#define B 0x00000003b9aca07bULL
#define SA (-0x0123456789abcdefLL)

int
muldiv64_check (void)
{
  CHECK (mul64 (A, B) == A * B);
  CHECK (smul64 (SA, 77) == SA * 77);
  CHECK (umul32x32 (0xfedcba98u, 0x89abcdefu) == (u64) 0xfedcba98u * 0x89abcdefu);
  CHECK (smul32x32 (-123456789, 987654321) == (s64) -123456789 * 987654321);
  CHECK (umulhi32 (0xfedcba98u, 0x89abcdefu) == (u32) (((u64) 0xfedcba98u * 0x89abcdefu) >> 32));
  CHECK (udiv64 (A, B) == A / B);
  CHECK (umod64 (A, B) == A % B);
  CHECK (udiv64 (A, 7) == A / 7);
  CHECK (sdiv64 (SA, 1000003) == SA / 1000003);
  CHECK (smod64 (SA, 1000003) == SA % 1000003);
  CHECK (udiv64_32 (A, 1000000007u) == A / 1000000007u);
  CHECK (udiv64_by10 (A) == A / 10);
  CHECK (umod64_by10 (A) == A % 10);
  CHECK (udiv64_by1000 (A) == A / 1000);
  CHECK (sdiv64_by7 (SA) == SA / 7);
  CHECK (udiv64_by_pow2 (A) == A / 4096);
  CHECK (udiv32_by10 (4000000000u) == 4000000000u / 10);
  CHECK (udiv32_by60 (3999999999u) == 3999999999u / 60);
  CHECK (sdiv32_by3 (-2000000001) == -2000000001 / 3);
  CHECK (umod32_by1000 (4294967295u) == 4294967295u % 1000);
  CHECK (udiv32 (4294967295u, 65537) == 4294967295u / 65537);
  CHECK (smod32 (-2000000001, 77) == -2000000001 % 77);
  CHECK (shl64 (A, 37) == A << 37);
  CHECK (shr64 (A, 33) == A >> 33);
  CHECK (sar64 (SA, 45) == SA >> 45);
  return failures;
}
