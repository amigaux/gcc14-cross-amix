# The toolchain question

OpenTTD is C++. AMIX's compiler is GCC 2.7.2.3, which predates the language
OpenTTD is written in and has no C++ front end installed at all. So before any
porting work is worth planning, one question has to be answered: **can a modern
compiler produce binaries that AMIX will run?**

It can. This document records the measurements, because every one of them is a
thing that could have been assumed wrongly, and each was cheap to check and
expensive to guess.

Target machine for all runtime results: Amiga 3000, Mercury 68060, AMIX SVR4
2.1c with the ported 68040/68060 kernel, 32 MB.

A note on identifying that kernel, because it caught us: **`uname` does not name
the image.** The port ships one image that boots both CPUs, with `cputype` poked
at boot from `AttnFlags`, and the prefix `uname -m` prints is a readout of the
CPU it is *running on* — not the build. So the same file reports
`68040-260903-04` in its own strings and `68060-260903-04` through `uname` on a
68060. Everything measured here was measured on image `68040-260903-04` running
on a 68060, and "the 68060 kernel" is not a thing that exists.

To identify an image for real, read a counter block's magic word at an address
computed from the candidate file — a method that fails loudly rather than
plausibly.

## 1. AMIX's ELF is the plain SVR4 ELF

Read off the machine's own `/usr/bin/ls`:

| Property | Value |
|---|---|
| Class / data | ELF32, big endian, `UNIX - System V`, machine `MC68000` |
| Interpreter | `/usr/lib/libc.so.1` — libc *is* the runtime linker |
| Text segment | `0x80000000` |
| Sections | `.interp .hash .dynsym .dynstr .rela.plt .init .plt .text .fini .data .got .dynamic .bss` |
| Dynamic tags | `NEEDED INIT FINI HASH STRTAB SYMTAB STRSZ SYMENT DEBUG PLTGOT PLTRELSZ PLTREL JMPREL` |
| `SYMENT` | 16 — the standard `Elf32_Sym` |
| Relocations | `R_68K_JMP_SLOT` |
| Segment alignment | `0x2000` |

Nothing here is unusual. There is no vendor extension to reproduce, no odd
symbol table layout, no pre-ELF relic. A modern linker can be told to emit
exactly this.

## 2. A modern compiler accepts the 1991 headers

GCC 14 compiles against `$AMIX_SYSROOT/usr/include` with `-nostdinc`, without a
single warning. The SVR4.0 headers are plain C89 with `__STDC__` guards, which is
the dialect modern GCC still reads.

## 3. A modern linker emits an AMIX-shaped executable

Linked with binutils 2.4x against AMIX's own `crt1.o`/`crti.o`/`crtn.o` and
`libc.so.1`, the result is structurally the same file. The PLT is the giveaway —
ours and the machine's are the same instruction sequence:

```
    jmp   %pc@(<got slot>)@(0)      ; 4efb 0171 ....
    movel #<reloc index>,%sp@-      ; 2f3c
    bral  <plt0>                    ; 60ff
```

The only differences were two extra program headers GNU tools add by habit:
`PT_GNU_RELRO` (removed with `-z norelro`) and `PT_GNU_STACK` (which modern `ld`
refuses to drop for this target — `-z nognustack` is ignored). **AMIX's loader
ignores the unknown segment**, verified by running the binary; see §5.

## 4. The m68k ABI: three differences, all fixed in the compiler

Compared by disassembling AMIX libc and gcc 2.7.2.3 output against GCC 14's
output for the same construct. "GCC 14" here is the `m68k-elf` compiler this
repository builds, *before* the fixes in the last column:

| Property | AMIX (AT&T cc, gcc 2.7.2.3) | GCC 14 `m68k-elf` | Fixed by |
|---|---|---|---|
| Pointer return | `%a0`, copied to `%d0`; **the caller reads `%a0`** | `%d0` only | `toolchain/amix-abi.h` |
| Struct return | always in memory, buffer address in `%a0` | 4 and 8 bytes in `%d0`/`%d1`; larger in memory, address in `%a0` | `toolchain/amix-abi.h` |
| Struct alignment | `int` and pointer members at 4 | at 2 | `-malign-int` in the specs (see *-malign-int is the ABI*) |
| `char` | signed | signed | — |
| `long double` | 96-bit extended, 12 bytes | 12 bytes | — |
| `float`/`double` return | `%fp0` | `%fp0` | — |
| `long long` return | `%d0`:`%d1` | `%d0`:`%d1` | — |

**What the two return faults look like.** Neither one is a crash at the
boundary; the caller reads a register that holds something else.

- A gcc 2.7.2.3 program calling a GCC 14 library function that returns a
  pointer gets whatever `%a0` last held. Seen by Jussi in October 2026:
  `zlibVersion()` returned garbage, and a 68030 took a SIGSEGV. The reverse
  direction happens to work, because AMIX puts the pointer in both registers.
- GCC 14 code calling libc's `div()` or `ldiv()` reads `div_t` from `%d0`/`%d1`,
  where libc never put it — and libc writes the real answer through whatever
  `%a0` held, which can be anything.

**The fix** is a target header, `toolchain/amix-abi.h`, which `build-cross.sh`
appends to the `m68k-*-elf*` `tm_file` in GCC's `config.gcc`. It does what the
m68k Linux target does for pointers and the NetBSD target does for both:
`FUNCTION_VALUE` becomes `m68k_function_value` (in `m68k.cc`, which returns a
pointer in `%a0` and `%d0` and has the caller read `%a0`), `%a0` joins the
value registers, and `DEFAULT_PCC_STRUCT_RETURN` is 1. It has to come after
`m68kemb.h`, which sets the embedded convention.

**Every GCC 14 library built before the fix has to be rebuilt** — libgcc and
libstdc++ included — because a new caller reads a returned pointer from `%a0`,
where an old callee never put it. `build-cross.sh` records the header's
checksum in `$TC_PREFIX/.amix-abi.sha256` and rebuilds GCC when it differs.

`probe/retabi.c` checks both faults in both directions, with its other half
built by gcc 2.7.2.3. Without the fix, the disassembly already shows the first
one: the old compiler's caller does `jsr g14_ptr` and then `cmpl %a0,%d2`,
while `g14_ptr` ends `addl %d1,%d0; rts`. Run on metal on 2026-10-27 —
`solon`, an Amiga 3000 with a Z3660 accelerator carrying a real 68060 (RC6),
kernel 68060-261003-08 — the same probe built both ways:

```
                                   without the fix   with it
pointer, gcc 2.7.2.3 -> GCC 14:    ok                ok
pointer, GCC 14 -> gcc 2.7.2.3:    WRONG             ok
struct, gcc 2.7.2.3 -> GCC 14:     WRONG             ok
struct, GCC 14 -> gcc 2.7.2.3:     WRONG             ok
div_t, libc div() -> GCC 14:       WRONG             ok
ldiv_t, libc ldiv() -> GCC 14:     WRONG             ok
```

The one line that passes without the fix is the direction that already
worked, because AMIX puts a returned pointer in both registers.

**Why this table once said "match".** Its first version said pointer return
matched, said the struct-return address was in `%a1`, and warned that
`-fpcc-struct-return` would not help. All three were true of the compiler it
was measured with: Debian's `m68k-linux-gnu-gcc-14`, the fallback C compiler,
before this repository moved to `m68k-elf`. The two targets differ exactly
there. GCC's `linux.h` uses `m68k_function_value` and keeps `m68k.h`'s `%a1`
struct-value register, while `m68kelf.h` moves that register to `%a0` and
`m68kemb.h` returns everything in `%d0`. On `m68k-elf`, `-fpcc-struct-return`
is in fact enough for structs, because the address is already in `%a0`. A
measurement belongs to the compiler it was taken with. When the compiler
changes, the table has to be measured again.

## 5. It runs on the real machine

```
$ tools/amix-cc.sh -o probe/hello -O2 probe/hello.c
$ tools/push-run.sh probe/hello
hello from a modern m68k gcc
  argc = 1, argv[0] = ./hello
exit status: 0
```

and the ABI battery (`probe/interop.c`), on the 68060:

```
sizes:   short=2 int=4 long=4 ptr=4 longlong=8
packing: sizeof=8 offsetof(i)=2   (SVR4 m68k wants 8 and 2)
char:    signed
mul64:   8641969
div64:   1234567890
heap:    libc string functions (len 21)
time:    tv_sec=832931896
fp:      1/3 = 0.333333
done
```

Three things worth naming in that output. The 64-bit results prove GCC's own
`libgcc` helper routines interoperate with our code. `heap`/`time` prove
`malloc`, the string functions and a syscall wrapper all cross the boundary
intact. And **`fp` proves the 68060 FPU path works** — including libc's
floating-point `printf`, which was the least certain item on the list, since the
68060 lacks instructions the 68881 has and the kernel's `ifpsp060` integration
is listed as incomplete.

## 6. What the packaged compiler cannot do

Debian ships `g++-14-m68k-linux-gnu`, and it is tempting. It cannot give us
working C++ exceptions:

```
$ m68k-linux-gnu-nm -u /usr/lib/gcc-cross/m68k-linux-gnu/14/libgcc_eh.a
    U _dl_find_object
    U pthread_cond_broadcast
    U pthread_key_create
    U pthread_mutex_lock
    ... (and the rest of the pthread family)
```

That is the glibc-flavoured unwinder: it finds exception tables by asking the
dynamic loader, and it locks with pthreads. AMIX has neither. An `m68k-elf`
target instead builds the *registry* unwinder, where `crtbegin` registers
`.eh_frame` from the `.init` section at startup and no loader cooperation is
needed; `--disable-threads` reduces the locking to no-ops. That is the reason
`toolchain/build-cross.sh` builds a compiler instead of installing one.

## 7. What the old compiler cannot do

Measured, not assumed:

- `m68k-cbm-sysv4-gcc -m68060` → ``cc1: Invalid option `68060' ``. Every line of
  C on this 68060 machine is currently compiled for a 68040 at best. The
  *assembler* (gas 2.8.1) does know the 68060.
- C99 is rejected: `for (int i = 0; ...)` → `parse error before 'int'`.
- No `cc1plus` is installed, and GCC 2.7.2.3's C++ predates the 1998 standard
  regardless.
- Its known miscompilations are already worked around by hand elsewhere in this
  project family: the Wolf3D AMIX build has to compile `wl_draw.c` with no `-O`
  because the optimiser hangs, and `wl_main.c` *with* `-O` because without it
  `InitGame` is generated wrongly.

For balance: on a tight hand-shaped loop the old compiler is not bad. Compiling
the 8bpp→RGB565 span an X11 video driver needs, GCC 2.7.2.3 produces five
instructions per pixel using a scaled index, GCC 14 produces six using
auto-increment. The case for modernising is correctness, language support and
68060 tuning — not per-instruction quality.

## 8. Library gaps to expect

AMIX is SVR4.0, which predates C99 and a good deal of what portable code
assumes. Confirmed absent from `libc.a`:

- `snprintf`, `vsnprintf` — OpenTTD uses these throughout
- `usleep`

Confirmed present: `gettimeofday`, `select`, `poll`, `mmap`, `shmget`, `strdup`,
`memmove`.

Also: the headers are incomplete against their own library. `<sys/time.h>`
declares `adjtime`, `getitimer` and `setitimer` but **not `gettimeofday`**, even
though libc exports it. A port has to declare what it uses.

## 9. The 68060 removes instructions libgcc uses

This one cost the most to find and matters beyond this port.

The compat layer's `snprintf` died on `%lld` and on nothing else. Two suspects
were cleared on the machine first: `va_arg(ap, long long)` reads the right eight
bytes even when the `va_list` was passed to another function, and 64-bit
division works. Then the second of those turned out to be an artefact of the
probe -- it divided by the *constant* 10, which GCC folds into a
multiply-by-reciprocal and never calls libgcc for at all.

Dividing by a variable is a different matter:

```
step 1: va_arg, no arithmetic
va_arg in callee:          hi=0000011f lo=71fb04cb  (want hi=0000011f lo=71fb04cb)
step 2: __udivdi3
Killed
```

The reason is in libgcc itself. Its `__udivmoddi4`, compiled for a 68020, uses

```
divul %d7,%d2,%d1        /* 32x64 divide */
divull %d4,%d3,%d3
mulul ...                /* 64-bit multiply */
```

and **the 68060 removed exactly those forms.** They are meant to be emulated by
Motorola's integer support package in the kernel; this machine's kernel has no
ISP handler wired in yet, so the unimplemented-instruction trap becomes a fatal
signal. `Killed` rather than a bus error, which is why it read as memory
exhaustion at first.

The fix is to tune for the 68060, and it has to be the compiler's **default**:
passing `CFLAGS_FOR_TARGET=-mcpu=68060` to a libgcc rebuild does not reach
libgcc's own compile -- measured, the instructions survived it. Tuned for a
68060, GCC avoids those forms everywhere, libgcc's `longlong.h` included.

The first fix was `--with-cpu=68060`, on the reasoning that it only ever
avoids instructions. For integer code that holds; for floating point it does
not. It makes the 68060 the architecture as well, and GCC then uses the
single- and double-rounding FPU forms the 68040 added (`fsmove`, `fdmove`,
`fdadd`, `fdmul` and the rest), which a 68881/68882 does not have. A runtime
built that way carries 491 of them in `libgcc.a` (`__floatundidf`, `__muldc3`,
`__powidf2`...) and 123 in `libstdc++.a` (`unordered_map`'s rehash policy,
`from_chars`, `hypotf`...), and a `make` that reached `__floatundidf` died with
SIGSYS on a 68030 + 68882. So the default is now `--with-cpu=m68020-60`
(the `m` is required; `config.gcc` knows the name only with it): the 68020
instruction set and the 68881 FPU, tuned for 68020 through 68060. That avoids
both, and `-m68060` remains an option for code that will only run on one.
`tools/check-68060-safe.py --68881` checks a binary or archive for both, and
`build-cross.sh` runs it on the installed runtime every time.

Worth noting for the kernel side of this project family: a 68060 without the
ISP cannot run *any* 68020-targeted C that divides 64-bit integers by a
variable, which includes anything compiled by a stock m68k toolchain. This is a
much larger blast radius than one printf conversion.

## 10. Building the toolchain: what the generic target does not know

`m68k-elf` is a bare-metal target and assumes newlib. Four things had to be
supplied or worked around; all four are in `toolchain/build-cross.sh` with the
reasoning attached.

- **`--disable-gcov`.** libgcov wants `MAP_ANONYMOUS` and `intptr_t`; SVR4.0 has
  neither, and the libgcc build stops in `_gcov_merge_topn.o`.
- **`--disable-multilib`.** The target otherwise builds about fifteen multilibs,
  nearly all ColdFire, each with its own libgcc and libstdc++. On a host with a
  few gigabytes free that fills the disk before the build ends.
- **A specs file** (`toolchain/install-specs.sh`). The stock specs are bare:
  startfile is just `crtbegin.o`, lib is just `-lc`, link is empty. So the
  compiler cannot link at all -- AMIX keeps its link-time `libc.so` in
  `usr/ccs/lib`, which is on no default search path -- and even if it could, the
  result would have no entry point, no interpreter and the wrong text address.
  A specs file is the supported way to retarget a GCC without patching it, and
  everything in ours is guarded on `%{!nostdlib:...}` so `tools/amix-cc.sh`,
  which names every object itself, is unaffected.
- **A one-case patch to libstdc++'s generated configure**
  (`toolchain/patch-libstdcxx-configure.py`). `crossconfig.m4` ends in a case
  that rejects unrecognised targets, and `m68k-elf` is not on its list. Since
  this is a freestanding configuration, the honest entry is an empty one.

A compiler that cannot link is not merely inconvenient: libstdc++'s configure
runs link tests, and a failure makes it set `GCC_NO_EXECUTABLES` and then abort
with *"Link tests are not allowed after GCC_NO_EXECUTABLES"*. Both drivers need
the specs -- the installed one, and the build tree's `xgcc`, which reads specs
from its own directory.

## 11. The C library gaps, measured

`snprintf` is absent from the real machine, not just from an old image. The
native linker on AMIX 2.1c says so:

```
Undefined                       first referenced
 symbol                             in file
snprintf                            /var/tmp/cca000Se1.o
```

`compat/amix_compat.c` supplies it, and borrows libc's own formatter one
conversion at a time rather than reimplementing formatting -- with one
exception. **libc's formatter does not understand C99's `ll`:**

```
snprintf(b, n, "%lld", 1234567890123LL)   ->  "287"
```

That cannot be worked around at the call site. A `long long` occupies eight
bytes of the argument list whatever conversion you write, so printing it as
`%ld` does not merely truncate the value, it leaves the formatter four bytes out
of step and every later argument comes out as garbage. So the `ll` conversions
are rendered by hand and everything else is delegated. It matters for OpenTTD
specifically: money is `int64`, and `stdafx.h` routes 64-bit output through
`OTTD_PRINTF64` / `OTTD_PRINTFHEX64`, both of which expand to `ll` forms.

The headers are also incomplete against their own library. `<sys/time.h>`
declares `adjtime`, `getitimer` and `setitimer` but not `gettimeofday`;
`<sys/select.h>` defines `fd_set` and the `FD_*` macros but does not declare
`select`. Both symbols are exported by libc. A port has to declare what it uses.

The formatter's test battery runs **both** natively and on the Amiga
(`tools/test-compat-host.sh`, `probe/compat.c`). That is not redundancy: the
battery passed on the host and died on the target, and that difference is what
localised the 68060 fault. Keep both.

## 12. C++ runs on AMIX

The gate, on the real machine:

```
$ tools/amix-cc.sh -o probe/cxxprobe -O2 probe/cxxprobe.cpp
$ tools/push-run.sh probe/cxxprobe
1 templates:  maximum(3,7)=7 maximum(2.5,1.5)=2.5
2 virtual:    area=36 (want 36)
3 new/delete: area=81 (want 81)
3 new/delete: destructor returned
4 throw/catch: about to throw from four frames down
4 throw/catch: caught, why="unwound from four frames down"
all four ok
```

Step 4 was the one genuine unknown, and it is the interesting result:
**DWARF exception unwinding works with no cooperation from the AMIX runtime
linker at all.** The exception tables are registered by `crtbegin.o` from the
`.init` section at startup, which is what an `elf` target's libgcc does when it
cannot rely on `PT_GNU_EH_FRAME` and a loader that understands it. That is the
reason this toolchain is built rather than installed, and it is now proven
rather than argued.

Two more things this required, both found the hard way.

**AMIX's C headers have no C++ linkage guards.** They are 1991 SVR4 headers: no
`#ifdef __cplusplus / extern "C"` anywhere. So a C++ file that includes
`<stdio.h>` gets a C++-mangled `printf` and does not link:

```
undefined reference to `_Z6printfPKcz'
```

That is not a corner case, it is every C header for every C++ file. The fix is
the one C++ compilers of that era used: `toolchain/make-cxx-wrappers.sh`
generates a wrapper per header, earlier on the include path, each opening
`extern "C"` and reaching the original with `#include_next`. 495 of them, from
whatever the installation actually has, because a hand-written list would
quietly miss one. Nothing in the vendor tree is modified and C compilation never
sees them.

**There is no `libgcc_eh.a` in a `--disable-shared` build** — the unwinder is
inside `libgcc.a`. `tools/amix-cc.sh` asks rather than assuming, so it works
against a toolchain built either way.

## 13. A hosted libstdc++, and what OpenTTD needs from it

The first working toolchain was `--disable-hosted-libstdcxx`, which was the
right first step: it builds `libsupc++` — `operator new`, RTTI, `__cxa_throw` —
and that is what the C++ gate needs. It is **not** enough for OpenTTD.

Measured in the 1.0.5 sources, outside `3rdparty` and the host-side build
tools: `std::map` in 18 files, `std::list` in 11, `std::set` in 2,
`std::vector` in 1, `std::string` in 2 (the tar reader), and 28 uses of
`dynamic_cast`. There are **no iostreams anywhere**, which is the good news:
iostreams are the part of libstdc++ that drags in locales, facets and wide
characters, and they are not needed.

A freestanding install does not carry those headers -- `<map>`, `<list>`,
`<set>`, `<string>`, `<vector>` and `<stdexcept>` are all absent -- and the
freestanding `libstdc++.a` carries none of the out-of-line parts the containers
call: no `_Rb_tree_insert_and_rebalance`, no `__throw_out_of_range`, no
`basic_string` instantiation. Header-only is not the same as
library-independent.

The hosted build now works, and `probe/stl.cpp` passes on the machine:

```
1 vector:   size=5 back=16
2 list:     size=3 sum=6
3 map (red-black tree, out of line in libstdc++):  size=20 m[3]=9
4 set:      size=2 first=1
5 string, and a map keyed by one:  "data/opengfx.grf" len=16  lookup=42
6 dynamic_cast (RTTI):  hit=99 miss=null
7 out_of_range from libstdc++, caught
all seven done
```

Getting there needed §14 — the header repairs — plus one more gap in the C
library itself. The Itanium C++ ABI expects `__cxa_atexit` from libc: the
compiler emits a call to it for every static object with a destructor. AMIX
knows only `atexit(void (*)(void))`, which cannot carry the object pointer, so
libstdc++ does not link. `compat/amix_cxa.c` supplies it, growing its list on
demand rather than using a fixed table, because silently dropping a destructor
past some arbitrary count is a bug that shows up as a file not flushed on a
machine nobody can attach a debugger to. Configuring GCC with
`--disable-__cxa_atexit` was the alternative; filling the gap in the C library
is preferred, because that is where the gap is, and everything else built with
this compiler gets working static destructors too.

## 14. Repairing headers that predate the language

Going from `libsupc++` to a **hosted** libstdc++ meant compiling libstdc++'s own
sources against a 1991 C library, and that is where AMIX's age stops being a
curiosity and starts being work. Four repairs were needed, and every one of them
would have hit OpenTTD too.

**K&R function definitions inside the headers.** AMIX implements `stat`,
`lstat`, `fstat`, `mknod` and `uname` as `static` functions defined in the
header itself -- wrappers around the versioned `_xstat` family -- written in
1978 syntax:

```c
static int
stat(path, buf)
const char *path;
struct stat *buf;
{ ... }
```

C has always accepted that. C++ never has, so **every C++ file that includes
`<sys/stat.h>` fails**, with errors that point at the system header and read as
if your program were at fault (`'int stat' redeclared as different kind of
entity`). Five headers in a stock installation do this.
`toolchain/make-kandr-fixes.py` rewrites them into ANSI form from the local
sysroot, into GCC's `include-fixed` -- the directory that exists precisely for
system headers that are wrong or older than the language, and which is searched
before the sysroot. The vendor tree is untouched, and the generated output
contains AMIX's own code so it never leaves the machine.

Two of those headers have a second fault, and that one breaks **C** too.
`<sys/mkdev.h>` declares `makedev`, `major` and `minor`, and `<ftw.h>` declares
`ftw`, with external linkage, and then defines them `static`. C has called that
undefined since C89. gcc 2.7.2.3 warns (`static declaration for 'makedev'
follows non-static`); GCC 14 stops. The fixer therefore also makes those
prototypes `static` (`make_static`). `<ftw.h>` was missed until October 2026,
because its definition has the return type on the signature line and a
function-pointer parameter (`int (*fn) ();`), and the converter skipped
anything it could not read. It now takes the parameter types from the header's
own prototype. That is also the right source for C++, where an empty `()`
means no arguments and would declare a second `ftw` that calls never reach.
The calls end up in libc's `__makedev`, `__major`, `__minor` and `_xftw`. Jussi
reported `mkdev.h`; `ftw.h` turned up while checking the fix.

**Missing C99 pieces**, supplied by hand-written wrappers in
`compat/include/fixed/` that reach the real header with `#include_next`:

| Missing | Where it bit | Repair |
|---|---|---|
| `FP_NAN`, `FP_INFINITE`, `FP_ZERO`, `FP_SUBNORMAL`, `FP_NORMAL` | libstdc++'s C++17 `to_chars` | tags for `__builtin_fpclassify`, which does the work itself -- they only have to be distinct |
| `ENOTSUP` | `std::filesystem` error paths | aliased to `EOPNOTSUPP`, which POSIX explicitly permits |
| `fsblkcnt_t`, `fsfilcnt_t` | `std::filesystem::space` | `unsigned long`, which is what AMIX's own `struct statvfs` fields are |

`<sys/statvfs.h>` also uses `u_long` without including `<sys/types.h>`, so it
compiles only if the translation unit happened to include that first; the
wrapper fixes the ordering trap while it is there.

**Configuration that assumes a modern target.** `std::filesystem` and
`std::stacktrace` are the parts of a hosted libstdc++ that reach hardest into
the operating system, and neither is anything a C++03 program wants.
`--disable-libstdcxx-filesystem-ts`, `--disable-libstdcxx-pch` and
`--disable-libstdcxx-backtrace` remove the parts that were still failing --
libbacktrace, given a target it could not classify, worked down its fallback
chain and settled on the **Windows** implementation, `GetModuleFileNameA` and
all.

**Telling libstdc++ what the target has.** The empty `m68k-*-elf*` case that got
the freestanding build past crossconfig is not enough for a hosted one:
libstdc++ then believes the target has no float maths and compiles its own
`fabsf`, which collides with AMIX's real one. The case now declares what the
target really has, and
`toolchain/patch-libstdcxx-configure.py` derives that list from the target's own
`<math.h>` rather than from anything typed by hand -- twenty functions, with the
`long double` variants deliberately left undeclared, because SVR4.0 genuinely
has none and libstdc++ *should* build its fallbacks for those.

## 15. No static constructor ran, and why nothing said so

With the library complete, OpenTTD compiled, linked and then died in
`std::_Rb_tree_increment` -- walking a `std::map` iterator -- with its own crash
log reporting `Blitter: none` and `Video driver: none`.

Those two lines are the clue. OpenTTD registers every driver and blitter by
constructing a global object whose constructor inserts into a static
`std::map`. If no constructor runs, the maps are never constructed either, and
the first iteration walks a tree of zeroes. A three-line probe confirmed it:

```
3 main: constructed=0 (want 1)      # and not one constructor line above it
```

**The cause is a disagreement about a section name, thirty years apart.**
AMIX's `crt1.o` calls `_init`, `crti.o` provides it, and GCC's `crtbegin.o`
contributes an `.init` fragment that walks `.ctors` -- all correct, and all
verifiable in the disassembly. But the modern linker script folds `.ctors` from
ordinary objects *into* `.init_array`:

```
KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
```

so the constructors land in `.init_array`, `.ctors` keeps only crtbegin's `-1`
and crtend's `0`, and `__do_global_ctors_aux` walks an empty list and returns.
On a system whose startup code knows about `.init_array` nobody notices,
because it walks that array too. AMIX's `crt1.o` is from 1991 and has never
heard of it.

Nothing anywhere reports this. It compiles, it links, it runs, and every global
object is quietly left zeroed.

`compat/amix_init.c` contributes its own `.init` fragment that walks
`__init_array_start .. __init_array_end`, and a matching `.fini` fragment for
the destructors. It is a **startfile** named in the specs rather than a member
of `libamixc99`, because a library member is pulled in only when something
references it and nothing ever references a constructor. `tools/amix-cc.sh`
adds it explicitly too: that wrapper passes `-nostdlib` and names every
startfile itself, so the specs' copy of the rule does not reach it.

After that, all four steps of the probe pass on the machine -- constructors
before `main`, in order, destructor after -- and OpenTTD 1.0.5 starts:

```
$ ./openttd -h
OpenTTD 1.0.5

Command line options:
  -v drv              = Set video driver (see below)
  ...
exit=0
```

## Conclusion

The toolchain is not a research risk, it is a build task. The ELF format, the
ABI, the startup path and the floating-point unit have all been checked against
the real machine, and the only mismatch found has a one-line fix (written
before the move to `m68k-elf`; section 4 now lists three). What remains
unproven is C++ specifically: `new`/`delete`, virtual dispatch, templates and —
the one genuine unknown — whether `try`/`catch` unwinds. That is what the next
probe answers.

## -malign-int is the ABI, and libc's __iob is the tripwire

This one cost an evening. It is written out at length because nothing about the
symptom points at the cause, and because the next person to port anything to
this system will meet it.

**The rule.** AMIX's `cc` aligns int and pointer members to 4 bytes and rounds
a struct to its widest member's alignment. m68k GCC rounds to 2. Measured with
AMIX's own compiler, and kept as `probe/abiprobe.c` so a compiler can be
checked against it rather than assumed:

| | AMIX cc | GCC, no `-malign-int` |
|---|---|---|
| `sizeof(FILE)` | **16** | 14 |
| `struct mixed { char a; short b; char c; int d; }` | 12, `d` at 8 | 10, `d` at 6 |
| `struct twoptr { char a; char *p; }` | 8, `p` at 4 | 6, `p` at 2 |
| `struct justchars { char a, b, c; }` | 3 | 3 |

The last row is the one that makes this a rule rather than a coincidence: an
all-char struct is **not** rounded up, so this is natural member alignment with
the struct rounded to its widest member — not a blanket round-to-4, which would
have produced 12 for `mixed` for the wrong reason.

**Why FILE is where it bites.** `/usr/include/stdio.h` defines `stdout` as
`(&__iob[1])`, so every call site does array arithmetic with its own idea of
`sizeof(FILE)`. libc's array is 320 bytes with `_NFILE` 20, a stride of 16. A
GCC-compiled call site computes `__iob + 14` for `stdout` and `__iob + 28` for
`stderr` — two bytes out and four bytes out, into the middle of the previous
FILE.

**What that looks like, which is the part worth remembering.** Not a crash:

- `printf` **works.** GCC turns it into `puts`, and libc indexes its own array
  correctly. So the program's help text prints perfectly.
- `fprintf(stdout, ...)` becomes `fwrite` on a FILE two bytes short of the real
  one. The flags it reads say "not writable", so it returns having written
  nothing, successfully.
- `fprintf(stderr, ...)` is four bytes out, so `_base` and `_ptr` are read from
  the middle of `__iob[1]`. Writing through those pointers stops the process
  and takes its login shell with it.

So a program prints its usage and then every diagnostic it would ever emit
disappears, and it exits 1 with zero bytes on both streams. OpenTTD did exactly
that for hours: `-h` printed 2300 bytes, and every other invocation died
silently. A redirected run leaves the file at **zero bytes**, which reads like
"stderr does not work" and is really "the write went somewhere else".

**How it was found**, since the route matters more than the answer:

1. `-h` works, everything else exits 1 with no output — so it is not the
   arguments, and stdout demonstrably works.
2. A probe bracketing each stdio call with `write(1, ...)` — a syscall, so it
   cannot buffer, cannot be reordered by a FILE, and does not depend on the
   thing under test. The last marker printed names the failing call. This is
   what turned "silent" into "`fprintf(stdout)` returns without writing and
   `fprintf(stderr)` hangs".
3. A native control on the machine: the same program built with AMIX's `cc`
   works completely. That moved the fault off the system and onto our binary.
4. `sizeof(FILE)` against `__iob`'s 320 bytes.

**The fix** is `-malign-int`, installed in the **specs** as a `self_spec`:

    replace("self_spec", "%{!mno-align-int:-malign-int}")

`self_spec` rather than `cc1`, because it rewrites the command line for every
language, so C, C++ and libgcc's own compiles get it from one place. It cannot
be a `CFLAGS` entry: one translation unit compiled without it disagrees with
every other about where a shared struct's fields are, and nothing says so at
link time. It is guarded on `-mno-align-int` so the fault can still be
reproduced deliberately.

**It is not enough to recompile the program.** libgcc and libstdc++ built
before this change are built to the other ABI, and any struct crossing that
boundary is laid out two ways — silently wrong rather than loudly hung. The
whole toolchain has to be rebuilt. `toolchain/build-cross.sh` already installs
the specs before `all-target-libgcc`, so a clean run gets this right.

### Still open: fprintf and `ll`

`compat/amix_compat.c` supplies `snprintf` and `vsnprintf`, which render `ll`
by hand because libc's 1991 formatter predates C99 and goes four bytes out of
step on a `long long`. It does **not** supply `fprintf`, so `fprintf` reaches
that formatter unpatched — and OpenTTD's `OTTD_PRINTF64` and
`OTTD_PRINTFHEX64` expand to `ll` forms because money is `int64`.

This has not bitten yet only because stdio was broken outright. It will bite
the moment something prints money through `DEBUG` or `usererror`. Recorded
here rather than fixed because the fix belongs with a measurement of what
actually breaks, and because a garbled number is a much friendlier failure than
the one above.

## Linking the 1991 libX11.a produces dynamic relocations that point nowhere

Unfinished, but far enough along to be worth writing down: **any binary that
links `libX11.a` carries hundreds of dynamic relocations whose offsets fall
outside the loaded image.**

    binary                       R_68K_32   COPY   JMP_SLOT
    no Xlib                             0      0          5
    references XOpenDisplay           856      1         62
    OpenTTD                          1308      3        115

The 856 have offsets from `0x0a` to `0x1f84` -- section-relative values that
were never rebased to a virtual address, in an image that starts at
`0x80000000`. Applying them writes into unmapped memory; ignoring them leaves
the data words they were meant to fill as zero.

Both failure modes are observed, and they explain two crashes that looked
unrelated:

- `probe/xref`, which merely takes the address of `XOpenDisplay` and never
  calls it, **segfaults before `main`** -- before its first `write(1, ...)`
  marker, which is a syscall and cannot buffer.
- OpenTTD starts, loads its graphics, generates a map, and dies at its first
  Xlib call with `Signal: Illegal Instruction` and **`pc=0x00000000`** -- a
  jump through a pointer that was never relocated.

**It is the modern linker that drops them.** A sibling AMIX port built one set
of GCC 14 objects twice against these same unrepaired archives, changing only
the linker:

| | ld 2.8.1 | modern ld |
|---|---|---|
| dynamic `R_68K_32` | 0 | 7453 |
| `.data` words holding a `0x800xxxxx` address | 66 | 1 |
| `.data` zero words | 963 | 2961 |

binutils 2.8.1 applies these relocations correctly. So the archives are
malformed by the letter of the ELF spec -- `sh_link` on a SHT_RELA section must
name the symbol table -- and the contemporary linker tolerated it anyway, which
is why thirty years of AMIX X clients have worked and why nobody had seen this.
The damage is GNU ld 2.43 disagreeing **silently**.

That comparison is better evidence than mine was, and the reason is worth
keeping: **a relocation count alone cannot settle it.** Zero dynamic
relocations is equally consistent with "resolved statically" and with "dropped
on the floor" -- which is exactly what makes this bug invisible. My own check
was 856 going to 0, and by that argument it was necessary but not sufficient.
What settles it here is the `.data` column above, and on this side the probe
that ran on the machine and printed `screen 0: 800x600 depth 16`. A repair
verified only by a number going to zero would not have been verified at all.

**A third route exists**, if the repair ever becomes inconvenient: compile with
GCC 14 at `--with-cpu=68060` and link with 2.8.1, where the defect never
arises. Not taken here, because this port needs C++ exception unwinding through
that link and whether it survives is an open question -- but it is the reason
the period toolchain was never exposed to this defect.

**The defect is universal, not merely common.** Of `libX11.a`'s 255 members,
236 contain `.rela` sections and **all 236 are wrong** -- 239 sections in
total. The remaining 19 are not exceptions; they simply carry no relocations.
That is the form the count belongs in: "236 of 255" invites the reader to ask
what is special about the other nineteen, and the answer is nothing. The
figures were confirmed independently against a read-only pristine AMIX mount,
by someone who went looking to disprove them.

`-lX11` alone accounts for all 856; `-lsocket` and `-lnsl` add none. The
symbols involved are libc *data*: `errno` (81 references), `__ctype` (61),
`_cleanup` (50), `getenv` (42), `_end` (37), `_etext` (29). In the SVR4 model
those need the executable to own a copy -- which is exactly what the `data.o`
member of `/usr/ccs/lib/libc.so` provides for `__iob`, and why stdio works at
all here. There is one COPY relocation (`errno`) where there should presumably
be several, and 856 per-reference relocations where there should be none.

**The lead to follow next**: `/usr/ccs/lib/libc.so` is an ar archive whose
members include `_ctype.o`, `errlst.o` and `data.o`. If the link pulls `data.o`
(for `__iob`) but not the members defining `__ctype` and friends, those symbols
stay dynamic and every reference to them becomes a relocation instead of being
bound to a local copy. Forcing those members in, or linking the matching parts
of `libc.a`, is the first thing to try.

### Two of my own measurements were wrong before this one was right

Worth recording because both produced confident nonsense:

- A control that linked `-lX11` and never referenced it proved nothing: a
  static archive yields only the members something refers to, so the binary was
  byte-identical to one linked without Xlib. Referencing a symbol through a
  function pointer is what actually pulls the library in -- 14 kB became 72 kB.
- `awk`'s `strtonum` does not exist in mawk. A range check written with it
  silently evaluated every address as 0, which turned "all offsets are below
  0x100" into a finding. The real range is 0x0a to 0x1f84, and the conclusion
  survived only because it was re-derived in Python.

## -mno-strict-align is the default

Unlike `-malign-int`, this flag changes speed, not the ABI. It is in the specs
as `%{!mstrict-align:-mno-strict-align}`, so `-mstrict-align` turns it back off.

**What it changes.** GCC's m68k back end assumes a 68000 unless told otherwise
(`m68k.cc` sets `MASK_STRICT_ALIGNMENT` for every non-ColdFire CPU, and the
`m68k-elf` target does not undo it the way `linux.h` does). A 68000 faults on a
misaligned word access, so GCC reads every `int` it cannot prove 4-aligned as two
16-bit halves joined with a shift and an `or`. That includes every `int`
argument, because the stack is only known to be 2-aligned. A function that
passes three small structs by value compiles to 42 instructions strict and 19
without. Every CPU AMIX runs on, from the 68020 to the 68060, does misaligned
access in hardware.

**What it does not change.** Measured on 31 sizes, offsets and alignments
(structs with chars, shorts, pointers, doubles, `long long`, bit-fields, unions,
nesting, `packed` and `long double`): identical under both flags. A struct passed
by value lands on the stack in the same image too; only the width of the moves
that build it changes. So objects built with and without the flag mix freely,
and libraries need no rebuild for it alone.

**Measured** by Jussi in October 2026 on a 68LC060 at 80 MHz in an A4000
(3 rounds of each build, spread under 1 %): Dhrystone 1.60x over the strict
GCC 14 build and 1.43x over gcc 2.7.2.3, which the strict build was about 11 %
*behind*. Memory, fork and context-switch figures stayed within 2 % across all
three. An emulated 68030 gave 1.6–1.8x. Floating point, which goes through the
060 kernel's FPU emulation, and `paranoia` passed in both GCC 14 builds.

**What it needs from the kernel, on a 68040.** A misaligned access that crosses
a page boundary is reported by the 68040 at the access's *start* address. A
kernel that does not handle that faults on the wrong page forever: an endless
`as_fault` loop that wedges the machine (040/060 port, ISSUE-37, found with
wolf3d). Port kernels from **68040-260728-26** on have the fix, accepted on 68040
metal. Strict code avoided misaligned word and long accesses, except through
packed data. Code built with this default can make them, so **run it on a
68040 only with a kernel at least that new.** Nobody has measured this flag on
68040 metal yet; the emulator is not evidence for the 68040's fault behaviour.
`probe/xpage.c` is the test for that day. It makes long and word reads, a
write, a read-modify-write and a compiled `memcpy`, each misaligned across a
boundary into a page that is not resident yet, and checks what came back.
Because it can wedge an older kernel, it refuses to run on a 68040 kernel
older than the fix. On 2026-10-27 it passed all nine steps on `solon` (68060
RC6 on a Z3660, kernel 68060-261003-08). That proves the probe itself, not
the 68040.

**One edge on the 68060.** `cas2` traps there at any alignment, because the 060
does not implement it, and so does a misaligned `cas` (040/060 port, ISSUE-79).
Either one ends the process: SIGKILL on the default kernel, and SIGSEGV with the
opt-in isp060 pass, by design. The flag leaves data layout alone, and GCC's
m68k back end never emits `cas2` (`sync.md` has only `cas`), so this default
creates no new case.

## Shared libraries: `gcc -shared`

```sh
m68k-elf-gcc -shared -fPIC -Wl,-soname,libfoo.so.1 -o libfoo.so.1 foo.c
m68k-elf-gcc -o prog prog.c -L. -lfoo -Wl,-rpath,/usr/local/lib
```

AMIX's runtime linker finds the library by `DT_RPATH` or `LD_LIBRARY_PATH`.
A program is linked the ordinary way and comes out exactly as before: the
specs change nothing unless `-shared` is given.

With `-shared` the driver links what an SVR4 `cc -G` links, with GCC's runtime
in PIC form:

| Start | | End |
|---|---|---|
| AMIX `crti.o`, `crtbeginS.o` | objects, `-lgcc` | `amix_initS.o`, `crtendS.o`, AMIX `crtn.o` |

It links no `crt1.o` (so no `_start`), no `libc.so` and no `libamixc99`. The
output has no interpreter, is linked at 0 as AMIX's own libraries are, and
gets the same `--hash-style=sysv -z norelro --build-id=none` as a program.
`--warn-textrel` reports an object compiled without `-fPIC`. AMIX loads such a
library, but each process then gets its own copy of the text.

**Constructors run during the program's start-up, not from `DT_INIT`.** The
runtime linker in `libc.so.1` calls each library's `DT_INIT` from `_rtld`. It
also registers a routine with `atexit` that calls each `DT_FINI` (`_rtld` and
`_rt_do_exit`, offsets 48 and 52 of a link-map entry). It never reads
`DT_INIT_ARRAY`, so `amix_initS.o` brackets the arrays itself (the `-shared`
linker script defines no `__init_array_start`; `compat/amix_init.c` says
how), and `_fini` walks `.fini_array`.

`_init` cannot walk `.init_array` there and then. `_rtld` runs the `DT_INIT`s
before `_rt_setup` calls `_rt_setaddr`. That function points 25 of libc's own
references at the program's copies of the data: `__iob`, `_lastbuf`, `_smbuf`,
`_bufendtab`, `errno`, `_environ`, `_fp_hw`, `__flt_rounds`, `__ctype`, the
`getopt` and timezone variables, `_cur_locale`, and `malloc`, `free`, `calloc`
and `realloc`. Until then the library already uses the program's copies and
libc still its own. A constructor's `printf` filled libc's private `stdout`,
its `fflush` flushed the program's, and the text never appeared. That was
measured on a 68030 and a 68060, though the constructors had run. `crt1.o` has
not set `_environ` yet either.

So `_init` hands the walk to `__amix_dso_init`. That function lives in
`libamixc99`'s `amix_dso.o`, which every library refers to, so the program's
link pulls it in and exports it. It keeps the libraries in the order the
runtime linker initialised them. The program's own start-up runs them from its
`.init_array` at priority 100: after `crt1.o` has set `_environ` and libc is
consistent, after libstdc++'s standard streams (priority 90), and before the
program's own constructors. Like those, they run before `__fpstart`. A library
that `dlopen` loads later runs at once. A program that uses no such library
does not link `amix_dso.o` and is unchanged.

**Weak references are safe.** `crtbeginS.o` refers weakly to
`__register_frame_info` and the `_ITM_` functions. AMIX's own libraries have no
weak symbols at all. Its runtime linker treats an unresolved `STB_WEAK` symbol
as 0 rather than failing (`do_reloc`, after the `_lookup` call).

**The C and C++ runtimes stay in the program.** AMIX's own libraries have no
`DT_NEEDED`, and a library built here NEEDs no runtime either. `libc.so` at link time
is an archive: `libc.so.1` plus static members built without PIC. What a
library leaves undefined, the program supplies. When the program links
against the library, `ld` pulls those symbols out of `libamixc99.a`,
`libc.so` or `libstdc++.a` and exports them: `snprintf`, `__cxa_atexit`,
`__gxx_personality_v0`. Five consequences:

- A program that uses a C++ library links with `g++`, even if the program
  itself is C. Otherwise `ld` stops with `undefined reference to
  __gxx_personality_v0`. A destructor that calls any C function needs it:
  AMIX's headers do not mark C functions as non-throwing, so the call inside
  a `noexcept` destructor gets an exception table.
- Build a C++ library with `gcc -shared`, or `g++ -shared -nostdlib++`. Plain
  `g++ -shared` adds `-lstdc++ -lm` and copies a static, non-PIC C++ runtime
  into the library, and `--warn-textrel` says so.
- A library opened with `dlopen` gets only what the program happens to export.
- Only a library named on the program's link line is filled in. A library
  that another one NEEDs, which `ld` finds with `-rpath-link`, is read after
  the archives have been searched, so what it leaves undefined stays
  undefined: libXmu NEEDs libXt, and a client that names only `-lXmu` fails
  with `undefined reference to gettimeofday`. The same holds for
  `libamixc99` (`snprintf`, `__cxa_atexit`) and `libstdc++.a`, and
  `--copy-dt-needed-entries` does not change it. For now a program names
  every library in its closure, as X11's `FixupLibReferences` does.
  `tools/test-shared-indirect.sh` reproduces it and fails until it is fixed.
- `libgcc` is linked into each library. Its members are hidden, and its
  integer helpers (`__divdi3` and the rest) have no absolute relocations.

**C++ static destructors run at exit, from `libamixc99`.** A library's static
objects register with `__cxa_atexit` like the program's own, into the one list
in the program. At exit one hook runs them all, newest first, before stdio's
`_cleanup`. `crt1.o` registers `_cleanup` just before it runs the program's
constructors, so the hook is registered by one of them, at priority 101. The
first `__cxa_atexit` call would be too early when a library's `DT_INIT` makes
it: the hook would run after the last flush. A library's `DT_FINI` calls
`__cxa_finalize` with the library's own handle, through `crtbeginS.o`. That
runs the library's entries and leaves everyone else's, so `dlclose` runs a
library's destructors, and at exit the call finds them already done.
`tools/test-compat-host.sh` checks which entries each call runs, and the exit
order against a stand-in `_cleanup`.

**Exceptions do not cross a library.** The unwinder is in `libgcc.a`, built
with hidden symbols, so each program has a private copy. A shared library
cannot register its `.eh_frame` with that copy, and an exception thrown in or
through a library's frames ends in `terminate`. Making that work needs a shared
`libgcc_s`, which is not built.

**Function addresses differ across the boundary.** Measured on AMIX 2.1c. When
a program takes the address of a library function, `ld` gives the program a
canonical address in its own PLT. AMIX's runtime linker resolves the library's
references to the library's own definition, so the two addresses compare
unequal. libXt's `_XtInherit` is the known case. X11's own fix also works
here: link the compared function into every client from a small static
archive, and the library's references then bind to the program's copy.

`tools/test-shared.sh` builds a C library with a constructor and a destructor
(`probe/solib-c.c`) and a C++ library with a static object
(`probe/solib-cxx.cpp`). It links two programs against them and checks the
results with `readelf`: `ET_DYN`, the `SONAME`, no interpreter, `DT_INIT` and
`DT_FINI`, no text relocations, nothing of the startup code exported, and
every undefined symbol defined by the program or `libc.so.1`. It stages
everything in `build/test-shared/` with `run-shared.sh`, to be copied to the
machine and run there.
