/* inlineasm.c -- user-mode inline asm with operands, of the kinds the
   kernel's C uses: data and address register operands, tied input/output,
   memory operands, early clobber, condition codes.  */
#include "rt.h"

NOIPA u32
asm_add (u32 a, u32 b)
{
  __asm__ ("add.l %1,%0" : "+d" (a) : "g" (b) : "cc");
  return a;
}

NOIPA u32
asm_swap (u32 a)
{
  __asm__ ("swap %0" : "=d" (a) : "0" (a) : "cc");
  return a;
}

NOIPA u32
asm_bfextu (u32 w, int off, int width)
{
  u32 r;
  __asm__ ("bfextu %1{%2:%3},%0" : "=d" (r) : "d" (w), "d" (off), "d" (width) : "cc");
  return r;
}

NOIPA void
asm_store (u32 *p, u32 v)
{
  __asm__ volatile ("move.l %1,%0" : "=m" (*p) : "d" (v));
}

NOIPA u32
asm_load_post (const u32 **pp)
{
  const u32 *p = *pp;
  u32 v;
  __asm__ ("move.l (%1)+,%0" : "=&d" (v), "+a" (p));
  *pp = p;
  return v;
}

NOIPA int
asm_cas (u32 *p, u32 old, u32 new)
{
  /* cas on an aligned long: implemented by the 68060 too.  */
  __asm__ volatile ("cas.l %0,%2,%1" : "+d" (old), "+m" (*p) : "d" (new) : "cc", "memory");
  return old;
}

NOIPA u32
asm_lea (u32 *base, int i)
{
  u32 *r;
  __asm__ ("lea (%1,%2.l*4),%0" : "=a" (r) : "a" (base), "d" (i));
  return *r;
}
