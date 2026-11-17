/* structret_check.c -- the callers, for a callee from either compiler. */
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

struct s1 ret1 (int);
struct s2 ret2 (int);
struct s3 ret3 (int);
struct s4 ret4 (int);
struct s6 ret6 (int);
struct s8 ret8 (int);
struct s12 ret12 (int);
struct s16 ret16 (int);
struct mixed retmixed (int);
char *retptr (int);
void *retvoidptr (void);
struct s8 pass_and_return (struct s8, struct s12);
unsigned sizes (void);

int
structret_check (void)
{
  struct s1 r1 = ret1 (0x41);
  struct s2 r2 = ret2 (0x1234);
  struct s3 r3 = ret3 (7);
  struct s4 r4 = ret4 (0x12345678);
  struct s6 r6 = ret6 (100);
  struct s8 r8 = ret8 (0x5a5a1234);
  struct s12 r12 = ret12 (1000);
  struct s16 r16 = ret16 (0x55);
  struct mixed rm = retmixed (0x77665544);
  struct s8 in = { 10, 20 };
  struct s12 more = { 1, 2, 3 };
  struct s8 rp = pass_and_return (in, more);
  char *p = retptr (5);
  CHECK (r1.a == 0x41);
  CHECK (r2.a == 0x1234);
  CHECK (r3.a == 7 && r3.b == 8 && r3.c == 9);
  CHECK (r4.a == 0x12345678);
  CHECK (r6.a == 100 && r6.b == 101 && r6.c == 102);
  CHECK (r8.a == 0x5a5a1234 && r8.b == ~0x5a5a1234);
  CHECK (r12.a == 1000 && r12.b == 2000 && r12.c == 3000);
  CHECK (r16.a == 0x55 && r16.b == 0x55 && r16.c == (s64) 0x55 << 33);
  CHECK (rm.a == 1 && rm.b == 2 && rm.c == 3 && rm.d == 0x77665544);
  CHECK (rp.a == 13 && rp.b == 19);
  CHECK (p == (char *) retvoidptr () + 5);
  CHECK (sizes () == (12u << 24 | 8u << 16 | 16u << 8 | 6u));
  return failures;
}
