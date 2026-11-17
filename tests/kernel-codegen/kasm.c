/* kasm.c -- supervisor inline asm and device-register access, compiled and
   disassembled only (it cannot run in user mode).  The shapes are the
   kernel's: spl-style status register changes, control registers through
   movec, alternate-space moves, and volatile byte/word/long access to a
   fixed address that must stay exactly one access of exactly that width.  */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

static inline u16
spl7 (void)
{
  u16 sr;
  __asm__ volatile ("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d" (sr) : : "memory");
  return sr;
}

static inline void
splx (u16 sr)
{
  __asm__ volatile ("move.w %0,%%sr" : : "d" (sr) : "memory", "cc");
}

u32
read_vbr (void)
{
  u32 v;
  __asm__ volatile ("movec %%vbr,%0" : "=r" (v));
  return v;
}

void
write_cacr (u32 v)
{
  __asm__ volatile ("movec %0,%%cacr" : : "r" (v) : "memory");
}

u32
fetch_user_long (const u32 *uaddr)
{
  u32 v;
  /* sfc is assumed set to user data space by the caller.  */
  __asm__ volatile ("moves.l (%1),%0" : "=d" (v) : "a" (uaddr) : "memory");
  return v;
}

int counter;

void
critical_increment (void)
{
  u16 s = spl7 ();
  counter++;
  splx (s);
}

#define CIA_PRA (*(volatile u8 *) 0xbfe001)
#define CUSTOM_INTENA (*(volatile u16 *) 0xdff09a)
#define ZORRO_REG (*(volatile u32 *) 0x40000010)

void
device_access (void)
{
  CIA_PRA |= 0x02;
  CUSTOM_INTENA = 0x7fff;
  ZORRO_REG = ZORRO_REG | 0x80000000u;
}

/* A read-modify-write of a 32-bit volatile register.  Each should stay a
   32-bit read and a 32-bit write; m68k's output_iorsi3, output_andsi3 and
   output_xorsi3 narrow a memory operand to the byte or word that changes,
   which is only right for ordinary memory.  */
void
zorro_rmw (void)
{
  ZORRO_REG |= 0x00000100u;	/* one bit: bset on a byte */
  ZORRO_REG &= ~0x00800000u;	/* one bit clear: bclr on a byte */
  ZORRO_REG |= 0x00001234u;	/* low half only: or.w on a word */
  ZORRO_REG ^= 0x00000001u;
}

u8
poll_ready (void)
{
  while (!(CIA_PRA & 0x40))
    ;
  return CIA_PRA;
}
