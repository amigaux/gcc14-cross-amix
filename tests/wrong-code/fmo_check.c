#include "rt.h"

struct rec
{
  int pad[16];
  int v;
};

void store_last (int *, int *, struct rec *, int);

NOIPA int
next_value (int count)
{
  return 1000 + count;
}

/* a[count - 1] gets s->v and b[count - 1] gets 1000 + count; every other
   element, a[count] above all, keeps its fill.  */
int
fmo_check (void)
{
  static int a[8], b[8];
  static struct rec s;
  int count, i;

  for (count = 1; count <= 6; count++)
    {
      for (i = 0; i < 8; i++)
	a[i] = b[i] = -1;
      s.v = 500 + count;
      store_last (a, b, &s, count);
      for (i = 0; i < 8; i++)
	{
	  CHECK (a[i] == (i == count - 1 ? 500 + count : -1));
	  CHECK (b[i] == (i == count - 1 ? 1000 + count : -1));
	}
    }
  return failures;
}
