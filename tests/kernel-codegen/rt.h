/* rt.h -- the little runtime the run cases need under qemu-m68k. */
#ifndef RT_H
#define RT_H
typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int u32;
typedef int s32;

#define NOIPA __attribute__((noipa))

int sys_write (int fd, const void *buf, unsigned len);
void put (const char *s);
void puthex (u32 v);
void *memcpy (void *d, const void *s, unsigned long n);
void *memset (void *d, int c, unsigned long n);

/* One failure line per wrong result; the case function returns the count. */
extern int failures;
void check (int ok, const char *what, int line);
#define CHECK(e) check ((e), #e, __LINE__)

int muldiv64_check (void);
int structret_check (void);
int manyargs_check (void);
int inlineasm_check (void);
int bitops_check (void);
int loops_check (void);
#endif
