# GCC 16 for AMIX kernel space

A second cross compiler, beside the GCC 14 one, for building the AMIX kernel:
C only, freestanding, no C library and no C++ runtime. It targets the same
form as GCC 14 — `m68k-elf` with `toolchain/amix-abi.h` and the same
compile-side specs — so one kernel tree can be built by both and the results
compared.

```sh
toolchain/build-gcc16.sh        # about 5 minutes on 32 cores; needs no config.sh
build/gcc16/prefix/bin/m68k-elf-gcc --version
# m68k-elf-gcc (gcc14-cross-amix GCC 16 kernel) 16.2.0
tools/test-kernel-codegen.sh    # the test set, under GCC 14 and GCC 16
```

**Route taken:** the signed GNU release GCC 16.2.0 with nine m68k fixes
(`toolchain/patches-gcc16/`), seven of them from the AmigaPorts `amiga16.2`
branch, rather than that branch itself. The branch does not build a compiler
for `m68k-elf` as it stands, and most of what it changes in the shared m68k
back end is AmigaOS work that would come along with it. The reasons are in
"The decision" below.

## Sources used

All fetched read-only on 2026-11-17.

| What | Where | Ref | Commit / sum |
|---|---|---|---|
| AmigaPorts toolchain build repo | https://github.com/AmigaPorts/m68k-amigaos-gcc | tag `v16.2-rc24` (2026-11-11) | `17984d6919fef67c9a94b1fd1d6b86c2e0714425` |
| AmigaPorts gcc | https://github.com/AmigaPorts/gcc | tag `m68k-amigaos-gcc-v16.2-rc24` on branch `amiga16.2` | `1dc78afff0993cd92c7c4770fc480419e385d3c6` (tag object `f1a9b03da60a2264d61a806ca44a81b68f687976`) |
| AmigaPorts binutils (not used) | https://github.com/AmigaPorts/binutils-gdb | tag `m68k-amigaos-gcc-v16.2-rc24` on `amiga-2.46` | `182a5100c88386b6f59b38c4d89eb150235f92c2` |
| Upstream GCC (git mirror) | https://github.com/gcc-mirror/gcc | `releases/gcc-16`, tag `releases/gcc-16.2.0`, `master` | 16.2.0 = `78d4ac73dd391005b895a6148cd9831e28e1208b` |
| GCC release | https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz | signature good (Richard Guenther's key, GNU keyring) | sha256 `e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e` |
| binutils release | https://ftp.gnu.org/gnu/binutils/binutils-2.46.1.tar.xz | signature good (Sam James's key, GNU keyring) | sha256 `e127a709cba24c76de8936cb7083dd768f28cd37eb010492e2f19b71eb1294e4` |
| Container recipe | https://github.com/reinauer/container-amiga-gcc | `main` | `4cd1d97fabc69c6e76f502c88b6841d9a297d8ef` |

The release studied is 16.2 rc24. A later rc25 (gcc tag object
`1b6fa961b75e8e338ec43e81b23bf74f65493c43`) appeared on 2026-11-16; it was not
studied.

`reinauer/container-amiga-gcc` builds the AmigaPorts toolchain for
`m68k-amigaos` (or `ppc-amigaos`) in a container by running that project's own
Makefile, optionally with every module at its `m68k-amigaos-gcc-TAG` tag. It
has no recipe for an ELF target to borrow; what it shows is the pinning
method (one release tag across all modules), which the pins above follow.

## What the AmigaPorts branch changes

`amiga16.2` at rc24 is upstream `releases/gcc-16` merged up to
`76bc3425a9e6e042e30d92edd005e6e0a2b023c8` (2026-10-13, after 16.2.0) plus
347 commits of its own (and 42 merges) since May 2026: 635 files, +42213
−628 lines. By the areas each commit touches (a commit can touch several):

| Area | Commits | For our target |
|---|--:|---|
| testsuite and CI only | 85 | irrelevant |
| AmigaOS target files (`m68kamigaos.h`, `amigaos.cc/.h/.opt`, `t-amigaos`, `driver-amigaos.cc`, `amigacollect2.cc`, `bbb-baserel.cc`, `simple-object-amiga`, `benaphore.h`, AmigaOS gthreads in libgcc) | about 75 | irrelevant: hunk object format, `-fbaserel`/`-resident` base-relative data in `a4`, `__saveds`, the libnix/clib2/newlib/ixemul runtime choice (`-mcrt=`), multilibs, SAS/C interoperation, AmigaOS threads |
| AmigaOS 4 / PowerPC (`rs6000`, SPE, libatomic, libssp) | about 30 | irrelevant |
| host and driver (libiberty, `gcc.cc`, `collect2`, path separators, `PROGDIR:`) | about 25 | irrelevant: AmigaOS as a *host* |
| C and C++ front ends | about 15 | AmigaOS language extensions (`__asm("d0")` register parameters, `#pragma amigaos`, calling-convention warnings) |
| shared m68k back end (`m68k.cc`, `m68k.md`, `m68k.h`, `m68k.opt`, `constraints.md`, `predicates.md`, new `m68k2.cc`, `m68k_costs.cc`) | about 85 | see below |
| middle end (`lra`, `cse`, `calls`, `function`, `ira`, `ivopts`, `tree-ssa-sink`, `bb-reorder`, `fold-mem-offsets`, `rtlhooks`, `ext-dce`) | about 20 | see below |
| libgcc m68k (`fpgnulib.c` long double, `lb1sf68.S`) | about 10 | soft-float long double; the kernel has no floating point |

The shared back end and middle end are where it matters for an ELF target,
because those files are compiled for every m68k target. They fall into three
groups.

**Features and retuning that change code for every m68k target.** The
argument-passing code moves into the new `m68k2.cc` (`m68k_init_cumulative_args`,
`m68k_function_arg`, register parameters with `-mregparm`); `FUNCTION_VALUE`
and `LIBCALL_VALUE` in `m68k.h` change for all targets (commit `c00fbf91d1`,
"fix ABI for proper long double/struct handling"); the default of `-mcpu=`
becomes `m68000` instead of "unset"; new cost tables (`m68k_costs.cc`), tuned
for the 68060 and `-m68020-60` with the 68040 tables (`0bc0352f30`); block
move and set expanders; target `doloop` hooks and a `dbra` emulation for
address registers; inline 32×32→64 multiplies for `-m68020-60` (`d38aee2446`);
prologues that skip saving call-saved registers that are only read
(`e2b7946086`); new sibling-call rules; and eight middle-end switches
(`TARGET_M68K`, `M68K_SW_ON`) that change `cse`, `gcse`, `ira` and `ivopts`
for m68k. Some of these are good work. Taken together they are a different
code generator from upstream's, designed and tested for `m68k-amigaos` only:
the branch's CI builds and tests `m68k-amigaos` and `ppc-amigaos`, and nothing
else.

**Fixes to those features.** A large share of the back-end commits from
September to November fix bugs in the features above: block move/set sizes
and copy direction (`4ac80755bc`, `569d72d67d`), a `cse` fix needed because the
new expanders emit auto-increment early (`0441d1ccaa`), constants that subtract
or negate a symbol under `-fbaserel` (`8a95039097`, `992967f31d`), sibling
calls with register arguments (a dozen commits), the `-Os` block loops. These
have no counterpart in upstream code.

**Fixes to code that upstream GCC 16 also has.** These are the ones worth
taking for any m68k target, and they are the patches in
`toolchain/patches-gcc16/` (below).

## Can the branch build for m68k-elf?

Not as it stands. Measured: the rc24 tree configured with
`--target=m68k-elf --enable-languages=c --without-headers --with-cpu=m68020-60`
fails to link `cc1`:

```
undefined reference to `m68k_init_cumulative_args(int*, tree_node*, tree_node*, bool)'   (33)
undefined reference to `m68k_function_arg_reg(unsigned int)'                             (24)
undefined reference to `m68k_handle_type_attribute(...)'                                  (9)
undefined reference to `m68k_costs(...)', `m68k_emit_movmemsi(...)', `m68k_function_arg(...)', ...
```

`m68k.h` and `m68k.cc` now call into `m68k2.cc` and `m68k_costs.cc`, which
`config.gcc` links only for `m68k*-*-amigaos*` (through `t-amigaos`). The
`m68k-*-linux*` fragment `t-linux` names cost files that no longer exist, so
that target is broken the same way.

With a one-file build fix — a `gcc/config/m68k/t-m68kelf`, a name `config.gcc`
already lists for `m68k-elf` but which does not exist, adding
`m68k2.o m68k_costs.o` — `cc1` links. libgcc then fails:
`libgcc/config/m68k/fpgnulib.c`, rewritten on the branch, includes
`<stdint.h>`, which a headerless `m68k-elf` build does not have. That trial
compiler (C only, no libgcc, with `amix-abi.h` and our specs added) was run
through the test set for comparison; its results are below.

## The decision

GCC 16.2.0 from ftp.gnu.org, plus nine patches. Reasons:

1. **The branch is an AmigaOS port.** Using it for `m68k-elf` means carrying
   our own ELF port of it (the build fix, a libgcc fix, and whatever the next
   rc breaks), and taking its whole shared back end — argument passing, return
   values, costs, middle-end switches — on code it was never tested with.
2. **It moves fast and fixes itself.** 347 commits since May; rc24 to rc25 in
   five days; many of the m68k fixes in rc24 are for bugs its own new features
   introduced. That is healthy for that project and wrong for a compiler whose
   job here is to be the steady second opinion beside GCC 14.
3. **The kernel needs differences it can explain.** With upstream 16.2.0 the
   code differs from GCC 14 by what upstream changed between 14 and 16, plus
   nine reviewed fixes. With the branch, every difference has a third source.
   The trial shows the scale: the 64-bit arithmetic case is 708 bytes under
   GCC 14, 730 under our GCC 16 and 1324 under the branch.
4. **Reproducible pins.** Two signed release tarballs and a small patch series
   in this repository, versus a GitHub tag of a fork.

What is given up: the branch's tuning and features, none of which a kernel
asks for, and any further upstream-relevant fix it finds — which is why the
fixes it has found so far were looked at one by one, and why a later rc can
be reviewed the same way. Following the branch instead remains possible; the
build fix above is what it takes to get a `cc1`, and on the run cases the
trial compiler interoperated with GCC 14 and GCC 16 in both directions.

## The patches

In `toolchain/patches-gcc16/`, applied in order to the release tree by
`build-gcc16.sh`. Each has a header naming its author, origin commit and
upstream status; testsuite files are left out. All apply cleanly (no offset,
no fuzz) to 16.2.0; 0005 and 0007 needed rebasing by hand.

| # | Fixes | Origin | Kernel relevance |
|---|---|---|---|
| 0001 | `move.l -(a0),(a0,d0.l)`: a move whose auto-inc/dec source register is used in the destination (GCC PR 123853, wrong code since GCC 15's combine) | AmigaPorts `4e957e9cc5`, Mikael Pettersson | copy loops |
| 0002 | the same for HI, QI and SF moves | AmigaPorts `0ce2e0f01b` | copy loops |
| 0003 | the same for DI, DF and XF moves | AmigaPorts `43928bfdcc` | 64-bit copies |
| 0004 | `subdi_dishl32` could emit a memory-to-memory `sub.l`, which does not exist (assembler error) | AmigaPorts `da450ba3e9` | 64-bit arithmetic |
| 0005 | DImode shift-by-32 splits read the source at the wrong address after storing the zero half through the same register | AmigaPorts `40fc1b0f03` | 64-bit shifts |
| 0006 | a `dbpl`/`dbmi` loop exited on `jle`, which also takes a zero result | AmigaPorts `209cea1723` | counted loops |
| 0007 | SImode `dbra` loops ended at the first borrow when the count was above 2^31; HImode counters in an address register compared all 32 bits | AmigaPorts `f831bb9945` | counted loops |
| 0008 | `bset`/`bclr` on memory use only the low 3 bits of the bit number; GCC modelled the full number (GCC PR 126121) | GCC master `b407be9e4b`, Andreas Schwab; not on `releases/gcc-16` or the AmigaPorts branch | bit flags in memory |
| 0009 | `fold-mem-offsets` folded an offset into the wrong operand of a memory-to-memory move | AmigaPorts `1e6714c8f3` | `-O2` |

Considered and left out:

| Commit | What | Why not |
|---|---|---|
| `63d9b4d849`, `5e493c3792`, `a15f0cf09c`, `0e35c650f6`, `05febfd36d`, `19d5126b92`, `712dcf2e94` | FPU register mode changes, FPCR scratch registers, soft-float long double | the kernel has no floating point; candidates if this compiler is ever used for userland |
| `b42a7d3d9c`, `1e9e983578`, `0ad086282b`, `68277b7a33` | LRA fixes | upstream 16.2.0 m68k still uses the old reload pass by default (`-mlra` is undocumented) |
| `0441d1ccaa` | `cse` and `REG_INC` notes | needed by the branch's early auto-increment expanders, not by upstream code |
| `094195e541` | `rtlhooks`, GCC PR 125728 | a candidate fix not yet in GCC; the trap it prevents is on a 68000 |
| `00daa894ef` | virtual registers in address operands | a generic change whose trigger was the branch's inline string code |
| `4cf818bd81` | narrowed MEM arguments on big-endian | its own message says the case is not reached in practice |
| `8a95039097`, `992967f31d` | symbol-subtracting constants | reached under `-fbaserel` only |

## The build

`toolchain/build-gcc16.sh` installs into `build/gcc16/prefix` (gitignored;
`GCC16_PREFIX` and `GCC16_WORK` override it) and touches nothing of the GCC 14
build. It:

- fetches the two tarballs into `build/gcc16/src` and refuses either if its
  sha256 differs from the pin; GCC's prerequisites (gmp, mpfr, mpc, gettext)
  come through its `contrib/download_prerequisites`, which checks them against
  the sha512 list inside the signed tarball;
- builds binutils 2.46.1 unpatched (the PIC relocation patch the GCC 14
  binutils carry is for shared libraries);
- extracts a fresh GCC tree, applies the patches, appends `amix-abi.h` to the
  `m68k-*-elf*` `tm_file` exactly as `build-cross.sh` does, and configures
  `--target=m68k-elf --enable-languages=c --without-headers --disable-multilib
  --with-cpu=m68020-60 --with-pkgversion="gcc14-cross-amix GCC 16 kernel"`;
- installs the compile-side specs before libgcc: `cpp` (`-D__amix__ -D__amix
  -Damix`), `cc1` (`-Wno-error=implicit-int`) and `self_spec`
  (`-malign-int -mno-strict-align`), the same entries as
  `toolchain/install-specs.sh` gives GCC 14. The link entries there are for
  AMIX programs and are not installed: a kernel links with `-nostdlib` and its
  own script;
- builds libgcc (`inhibit_libc`), which is what a kernel links for 64-bit
  arithmetic, and checks it with `tools/check-68060-safe.py --68881`:
  137 long multiply/divide instructions, none in a 64-bit form; 1555 FPU
  operations, none in a 68040/060-only form;
- rebuilds GCC only when the tarball, a patch, `amix-abi.h` or the configure
  line changes.

Two builds from an empty `build/gcc16` on 2026-11-17 (3 min 48 s the second
time) produced a bit-identical driver and `cc1` (sha256 `394b348e…` and
`5e8175f3…`); `libgcc.a` differed between them, which was not investigated.

## The test set

`tests/kernel-codegen/` (see its README) and `tools/test-kernel-codegen.sh`.
Flags for both compilers: `-O2 -m68020-60 -msoft-float -ffreestanding
-fno-common -std=gnu99`. Result on 2026-11-17, GCC 14.2.0 (`build/prefix`) and
GCC 16.2.0 (`build/gcc16/prefix`):

```
ok   gcc14: all C cases compile, sup030/040/060.s assemble
ok   gcc14: no 64-bit mulu.l/divu.l, no 68040/060-only FPU forms in the C objects
ok   gcc14: supervisor instructions disassemble as written
ok   gcc16: (the same three)
     both: external calls from the cases: __divdi3 __moddi3 __muldi3 __udivdi3 __umoddi3
FAIL run callees=gcc14 callers=gcc14 on m68030/m68040/m68060   (bitops only)
FAIL run callees=gcc14 callers=gcc16 on m68030/m68040/m68060   (bitops only)
ok   run callees=gcc16 callers=gcc14 on m68030/m68040/m68060
ok   run callees=gcc16 callers=gcc16 on m68030/m68040/m68060
```

The six failures are one case, `bitops`, whenever GCC 14 compiled the callee:
`setbit_char (&a, 10)` must leave `a` alone (`(char) (1 << 10)` is 0) and
GCC 14 emits `bset %d0,(%a0)`, which sets bit 2. That is GCC PR 126121,
present in GCC 14.2.0 and in the AmigaPorts branch, fixed here by patch 0008.
The other five run cases pass in all four combinations on all three CPUs,
which shows both compilers agree on the calling convention for everything the
cases cover (stack arguments of every width, variadic arguments, struct
returns of 1 to 16 bytes, pointer returns, 64-bit values). qemu's 68060 traps
the 64-bit `mulu.l`/`divu.l` as the real CPU does (checked with a one-line
`divu.l %d2,%d0:%d1`), so those runs also cover both libgccs' 64-bit helpers.

### What differs between GCC 14 and GCC 16

| Case | .text bytes 14 → 16 | Difference |
|---|---|---|
| muldiv64 | 708 → 730 | Same libcalls in both: `__muldi3` for every 64-bit product, the 32×32→64 widening ones included (zero- or sign-extended to 64 bits first), and `__[u]divdi3`/`__[u]moddi3` for every 64-bit division and remainder, by constants too. 32-bit division and remainder by a constant use the 32-bit `divu.l`/`divs.l` (the form the 68060 keeps), no reciprocal multiply, in both. Variable 64-bit shifts are four instructions longer in GCC 16 (an extra stack slot). |
| structret | 214 → 214 | identical code: struct returns through the buffer in `%a0`, pointer returns in `%a0` and `%d0` |
| manyargs | 236 → 250 | `many`: identical. `vsum`: `va_arg` code arranged differently, same layout. |
| inlineasm | 98 → 98 | identical |
| bitops | 62 → 74 | the PR 126121 fix: `bset` with a variable bit becomes shift + `or.b`; `bclr` with `b & 7` becomes `bfclr` with a computed offset (correct, two instructions longer) |
| loops | 122 → 114 | `sum_n` closes with `dbf` and the 32-bit borrow fix-up (`clr.w`, `subq.l`, `bcc`) in both; that comes from the `decrement_and_branch` patterns, which were already right. The other loops use no `dbra`. GCC 16's loop set-up is shorter. |
| kasm | 204 → 116 | **volatile read-modify-write is merged and narrowed**, see below |

**Volatile 32-bit registers are written as bytes and words by GCC 16.** For
`volatile u32 *r`, `*r |= 0x100` is `move.l (a0),d0; or.w #256,d0;
move.l d0,(a0)` under GCC 14 and `or.w #256,2(a0)` under GCC 16;
`*r &= ~0x800000` becomes `bclr #7,1(a0)`, and `*r |= 0x80000000` becomes
`bset #7,(a0)`. Both compilers keep each access volatile and in order, but
GCC 16 now folds the load, operation and store into one memory instruction,
and m68k's `output_iorsi3`/`output_andsi3`/`output_xorsi3` then narrow a
memory operand to the byte or word that changes — a 32-bit device register is
read and written with an 8- or 16-bit access. The narrowing code is
upstream's and none of the nine patches touches it; the AmigaPorts trial
compiler does the same. For a register that must be accessed 32 bits wide
this is a real change. Options: access such registers through explicit
accessors (inline asm or a `move.l` helper), or a small back-end patch that
skips the narrowing when the memory operand is volatile. Not decided here.

### The AmigaPorts trial, for comparison

The same test set with the trial compiler as a third: everything compiles and
assembles, no 64-bit `mulu.l`/`divu.l`, the run cases pass against GCC 14 and
GCC 16 in both directions except `bitops` (PR 126121 is not fixed there).
Its code is larger where its new features act: muldiv64 1324 bytes (inline
reciprocal multiplies for 64-bit division by constants, 62–76 instructions
each instead of a libcall; inline 32×32→64 multiplies), manyargs 258,
structret 204, and it does use `dbra` for `fill` and `count_until`.

## Licences

| Component | Licence |
|---|---|
| GCC 16.2.0 | GPL-3.0-or-later; libgcc under the GPL with the GCC Runtime Library Exception, so a kernel may link it |
| binutils 2.46.1 | GPL-3.0-or-later |
| AmigaPorts/gcc | GPL-3.0-or-later, as GCC (the sources carry GCC's headers; the build repo's COPYING-THIRD-PARTY.md calls GCC "GPL V2", which is not what the files say) |
| `toolchain/patches-gcc16/*.patch` | GPL-3.0-or-later, as the GCC files they change (each header says so); not covered by this repository's MIT licence |
| AmigaPorts/m68k-amigaos-gcc (build repo) | GPL-2.0 (its COPYING); nothing taken from it |
| reinauer/container-amiga-gcc | no licence file found; read as a reference, nothing taken |

## Not determined

- The kernel's own flags. The test set uses `-O2 -msoft-float -ffreestanding
  -std=gnu99`; a kernel build chooses its own, and a K&R kernel tree will
  want an older `-std` and probably `-fno-strict-aliasing`.
- Whether `-malign-int` and `-mno-strict-align` belong in a kernel compiler's
  specs. They are there for parity with GCC 14, whose specs force them; the
  kernel's own structure-layout checks decide struct layout, and a kernel built
  with `-mstrict-align` or `-mno-align-int` can say so on the command line.
- The `dbra` fixes (0006, 0007) are taken on their reasoning and the branch's
  tests. They change `output_dbcc_and_branch` (the `dbCC` peepholes) and the
  HImode address-register fallbacks, which the test set's loops did not reach
  under either compiler; its one `dbra` loop comes from patterns that were
  already right.
- Patches 0001–0003, 0005 and 0009 have no reproducer in the test set; their
  triggers are combinations of late-combine and auto-increment that small
  cases do not produce reliably.
- Nothing has run on hardware or under Amiberry; the runs are qemu user-mode
  on its 68030, 68040 and 68060 models.
- rc25 and later AmigaPorts releases have not been reviewed for further
  upstream-relevant fixes.
- Whether the volatile narrowing above affects any register the AMIX kernel
  touches; that is for the kernel's own build to check.
