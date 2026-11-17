/* structret.c -- AMIX returns every struct in memory, through a buffer whose
   address the caller passes in %a0 (DEFAULT_PCC_STRUCT_RETURN in
   toolchain/amix-abi.h), and a pointer in %a0 with a copy in %d0, the caller
   reading %a0.  Sizes 1 to 16 bytes, odd and even.  */
#include "rt.h"

struct s1 { char a; };
struct s2 { short a; };
struct s3 { char a, b, c; };
struct s4 { int a; };
struct s6 { short a, b, c; };
struct s8 { int a, b; };
struct s12 { int a, b, c; };
struct s16 { int a; char b; s64 c; };
struct mixed { char a; short b; char c; int d; };

NOIPA struct s1 ret1 (int x) { struct s1 r = { x }; return r; }
NOIPA struct s2 ret2 (int x) { struct s2 r = { x }; return r; }
NOIPA struct s3 ret3 (int x) { struct s3 r = { x, x + 1, x + 2 }; return r; }
NOIPA struct s4 ret4 (int x) { struct s4 r = { x }; return r; }
NOIPA struct s6 ret6 (int x) { struct s6 r = { x, x + 1, x + 2 }; return r; }
NOIPA struct s8 ret8 (int x) { struct s8 r = { x, ~x }; return r; }
NOIPA struct s12 ret12 (int x) { struct s12 r = { x, x * 2, x * 3 }; return r; }
NOIPA struct s16 ret16 (int x) { struct s16 r = { x, (char) x, (s64) x << 33 }; return r; }
NOIPA struct mixed retmixed (int x) { struct mixed r = { 1, 2, 3, x }; return r; }

static char buf[32];
NOIPA char *retptr (int i) { return buf + i; }
NOIPA void *retvoidptr (void) { return buf; }
NOIPA struct s8 pass_and_return (struct s8 in, struct s12 more)
{
  struct s8 r = { in.a + more.c, in.b - more.a };
  return r;
}
NOIPA unsigned sizes (void)
{
  /* AMIX's layout, from -malign-int: mixed is 12 bytes with d at 8.  */
  return sizeof (struct mixed) << 24 | __builtin_offsetof (struct mixed, d) << 16
	 | sizeof (struct s16) << 8 | sizeof (struct s6);
}
