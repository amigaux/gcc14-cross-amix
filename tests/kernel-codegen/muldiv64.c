/* muldiv64.c -- 64-bit arithmetic as a kernel does it.  The 68060 has no
   32x32->64 MULU.L/MULS.L and no 64/32 DIVU.L/DIVS.L, so under -m68020-60
   neither compiler may emit them: a multiply has to be built from 32-bit
   ones, a division by a variable has to call libgcc, and a division by a
   constant becomes a multiply by its reciprocal.  */
#include "rt.h"

NOIPA u64 mul64 (u64 a, u64 b) { return a * b; }
NOIPA s64 smul64 (s64 a, s64 b) { return a * b; }
NOIPA u64 umul32x32 (u32 a, u32 b) { return (u64) a * b; }
NOIPA s64 smul32x32 (s32 a, s32 b) { return (s64) a * b; }
NOIPA u32 umulhi32 (u32 a, u32 b) { return ((u64) a * b) >> 32; }

NOIPA u64 udiv64 (u64 a, u64 b) { return a / b; }
NOIPA u64 umod64 (u64 a, u64 b) { return a % b; }
NOIPA s64 sdiv64 (s64 a, s64 b) { return a / b; }
NOIPA s64 smod64 (s64 a, s64 b) { return a % b; }
NOIPA u64 udiv64_32 (u64 a, u32 b) { return a / b; }

NOIPA u64 udiv64_by10 (u64 a) { return a / 10; }
NOIPA u64 umod64_by10 (u64 a) { return a % 10; }
NOIPA u64 udiv64_by1000 (u64 a) { return a / 1000; }
NOIPA s64 sdiv64_by7 (s64 a) { return a / 7; }
NOIPA u64 udiv64_by_pow2 (u64 a) { return a / 4096; }

NOIPA u32 udiv32_by10 (u32 a) { return a / 10; }
NOIPA u32 udiv32_by60 (u32 a) { return a / 60; }
NOIPA s32 sdiv32_by3 (s32 a) { return a / 3; }
NOIPA u32 umod32_by1000 (u32 a) { return a % 1000; }
NOIPA u32 udiv32 (u32 a, u32 b) { return a / b; }
NOIPA s32 smod32 (s32 a, s32 b) { return a % b; }

NOIPA u64 shl64 (u64 a, int n) { return a << n; }
NOIPA u64 shr64 (u64 a, int n) { return a >> n; }
NOIPA s64 sar64 (s64 a, int n) { return a >> n; }
