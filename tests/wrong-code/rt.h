/* rt.h -- the little runtime the wrong-code cases need under qemu-m68k. */
#ifndef RT_H
#define RT_H
typedef unsigned int u32;

#define NOIPA __attribute__((noipa))

int sys_write (int fd, const void *buf, unsigned len);
void put (const char *s);
void *memcpy (void *d, const void *s, unsigned long n);
void *memset (void *d, int c, unsigned long n);

/* One failure line per wrong result; the case function returns the count. */
extern int failures;
void check (int ok, const char *what, int line);
#define CHECK(e) check ((e), #e, __LINE__)

int byteclr_check (void);
int fmo_check (void);
#endif
