/* fmo.c -- two stores indexed by the same count - 1.  GCC 14.2.0's
   fold-mem-offsets pass, at -O2, folds the -4 of (count - 1) * 4 into the
   second store's offset and deletes the subtraction, but the first store
   is a memory-to-memory move indexed by the same register and keeps no
   offset, so it writes a[count]:

	move.l 64(%a1),(%a0,%d0.l)	| d0 = count * 4
	...
	move.l %d0,-4(%a0)

   Reduced from CDE's DtSvc (MMDb.c, build_file_list).  */
#include "rt.h"

struct rec
{
  int pad[16];
  int v;
};

int next_value (int);

NOIPA void
store_last (int *a, int *b, struct rec *s, int count)
{
  a[count - 1] = s->v;
  b[count - 1] = next_value (count);
}
