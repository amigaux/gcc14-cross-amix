# Kernel code-generation test set

Small freestanding cases that compare what two compilers make of the kernel's
kind of C: GCC 14 (`toolchain/build-cross.sh`) and GCC 16
(`toolchain/build-gcc16.sh`). Run it with

    tools/test-kernel-codegen.sh

or name the compilers, the first two being the pair whose code is diffed:

    tools/test-kernel-codegen.sh gcc14p=build/prefix-pr126121/bin/m68k-elf-gcc \
        gcc14g=build/prefix-bfguard/bin/m68k-elf-gcc \
        gcc11=build/prefix/bin/m68k-elf-gcc \
        gcc16p=build/gcc16/prefix/bin/m68k-elf-gcc \
        gcc16g=build/gcc16/prefix-bfguard/bin/m68k-elf-gcc

(the GCC 14 builds: `build/prefix` unpatched, as 1.1 was built;
`build/prefix-pr126121` with the bset/bclr patch; `build/prefix-bfguard` with
that and the bit-field guard. GCC 16: `build/gcc16/prefix` with 0001-0009,
`build/gcc16/prefix-bfguard` with 0010 too.)

Every case is compiled by each compiler with the same flags (`-O2 -m68020-60
-msoft-float -ffreestanding -fno-common -std=gnu99`; set `CFLAGS` to change
them), disassembled, scanned for the GCC PR 126121 pattern
(`tools/check-bset-mem.py`, below), and diffed between the compilers. The
cases that can run are then linked four ways -- each compiler's callees with
each compiler's callers -- and run under `qemu-m68k` user emulation on a
68030, a 68040 and a 68060, which checks results and the calling convention
across the compilers at once. Output goes to `build/gcc16/codegen/`.

| File | What it holds | Run |
|---|---|---|
| `crt0.s`, `rt.c`, `rt.h` | entry point and output through Linux m68k system calls, for qemu only | -- |
| `muldiv64.c`, `muldiv64_check.c` | 64-bit multiply, divide and remainder by variables and by constants, 32x32->64 widening multiply, 32-bit division by constants | yes |
| `structret.c`, `structret_check.c` | struct returns of 1 to 16 bytes, a pointer return | yes |
| `manyargs.c`, `manyargs_check.c` | a function of twelve mixed arguments, a variadic one | yes |
| `inlineasm.c`, `inlineasm_check.c` | user-mode inline asm with register, tied, memory and address operands | yes |
| `bitops.c`, `bitops_check.c` | `bset`/`bclr` on memory with a variable bit number (GCC PR 126121) | yes |
| `loops.c`, `loops_check.c` | counted loops the back end turns into `dbra`, with 32-bit counters | yes |
| `byteclr.c`, `byteclr_check.c` | a byte's bit cleared, set and flipped with bit numbers 0..7 and 8, 10, 15, 30, and a bit array, all checked for stray writes in the bytes around (the bit-field form PR 126121's fix leaves) | yes |
| `kasm.c` | supervisor inline asm (`movec`, `move sr`, `moves`), volatile device-register access | compile only |
| `sup030.s`, `sup040.s`, `sup060.s` | supervisor instructions, assembled with `-m68030`, `-m68040`, `-m68060` | assemble only |

qemu's 68060 model traps the 64-bit `mulu.l`/`divu.l` forms the real 68060
lacks (measured with qemu 10.2.1: `divu.l %d2,%d0:%d1` runs on its 68030 and
68040 and is an illegal instruction on its 68060), so the 68060 run also
covers the libgcc helpers the cases call. The C objects are in addition
checked from their encodings with `tools/check-68060-safe.py`.

## The PR 126121 scan: `tools/check-bset-mem.py`

BSET and BCLR with the bit number in a register and a memory operand use only
its low three bits; GCC 14.2.0 without
`toolchain/patches/gcc-0001-bset-bclr-mem-bit-number.patch` (the 1.1 compiler) and
GCC 16.2.0 without `0008` assumed the full number, so
`*p |= (unsigned char) (1 << n)` and `*p &= ~(1 << n)` with `n` of 8 or more
change bit `n & 7` where C changes nothing; with that patch alone,
`*p &= ~(1 << n)` becomes a `bfclr` that writes the byte before `*p`, which
`gcc-0002-bitfield-mem-offset-guard.patch` (GCC 16: `0010`) removes.
`docs/TOOLCHAIN.md`, "GCC carries two patches", has the details. The scan finds that code after the fact, in
anything a build produces, and is meant to be run as a gate:

    tools/check-bset-mem.py [--objdump PATH] [--allow FUNC]... [-q] FILE|DIR ...

| Input | How |
|---|---|
| `foo.s` from `gcc -S` | read as GCC prints it (Motorola syntax with `%`) |
| `foo.o`, `libfoo.a`, a program, a `.so` | disassembled with an m68k objdump: `--objdump`, `$OBJDUMP`, `config.sh`'s `TC_PREFIX`, `build/prefix`, then `m68k-elf-objdump` on `PATH` |
| `foo.dis` (`objdump -d` output) | read as it is |
| a directory | every `*.s`, `*.o`, `*.a`, `*.so*`, `*.dis` under it |

It exits 0 when nothing is flagged, 1 when something is (each site with file,
address or line, function, instruction and reason), and 2 when an input cannot
be read or is not one of the above -- never a clean verdict for something it
did not understand. It flags:

- `bset`, `bclr` or `btst` with `%dN` as the bit number and a memory operand,
  unless `%dN` is shown to be 0..7 within the basic block (`moveq #k`,
  `and.l` with a non-negative constant or a register holding one, `clr.l`,
  copies, sums and differences of known values);
- a bit-field instruction (`bfclr`, `bfset`, `bfchg`, `bfins`, `bftst`,
  `bfextu`, `bfexts`, `bfffo`) on memory whose offset register was computed as
  `7 - x` with `x` not shown below 8: the form the first patch leaves, see
  below.

A finding is a site the scan cannot prove safe, not proof of a fault: a number
masked in another basic block is not seen. Name a function that has been read
and found right with `--allow FUNC`. objdump decodes GCC's switch tables in
`.text` as instructions; the scan skips each table (length from the bounds
check before the `jmp`) and decodes again from the first real instruction when
objdump ran the table into it, so nothing real is hidden. Over the same C
compiled to `.s` and to `.o` (this set, `compat/*.c` and a switch-heavy probe,
at `-O1`, `-O2`, `-O3` and `-Os`, both compilers) it flags the same sites.

`tools/test-kernel-codegen.sh` runs it over each compiler's case objects as
step 4. On `bitops.c` (2026-11-18):

| Compiler | `bitops.s` | `bitops.o` |
|---|---|---|
| GCC 14.2.0 unpatched (`build/prefix`, the 1.1 compiler) | exit 1: `setbit_char`, `bset %d0,(%a0)` | exit 1: `setbit_char` 0x8, `bset %d0,%a0@` |
| GCC 14.2.0 with the bset/bclr patch (`build/prefix-pr126121`) | exit 0 | exit 0 |
| GCC 14.2.0 with both patches (`build/prefix-bfguard`) | exit 0 | exit 0 |
| GCC 16.2.0 with 0008, and with 0008 and 0010 | exit 0 | exit 0 |

On `byteclr.c` the unpatched compiler is flagged for `clr_byte`,
`clr_byte_u8` (`bclr`) and `set_byte` (`bset`), the bset/bclr patch alone
(GCC 14 or 16) for `clr_byte` and `clr_byte_u8` (`bfclr` at `7 - n`), and
both patches for nothing. Over the cases, `compat/*.c` and two probes at
`-O2` that is 36, 19 and 0 sites, the same in `.s` and `.o`.

`clrbit_char` is not flagged under the unpatched compiler: its `bclr` follows
`moveq #7,%d0; and.l 8(%sp),%d0`, so the number is below 8 and the code is
right.

**The form the first patch leaves, and the guard.** `*p &= ~(1 << n)` on a
byte, with the bset/bclr patch alone, is compiled to `moveq #7,%d0; sub.l
n,%d0; bfclr (%a0){%d0:#1}`; for `n` of 8 or more the offset is negative and
BFCLR clears a bit in the byte before `*p`. The guard patch lets the memory
bit-field patterns take only a constant, non-negative offset, and the byte is
then updated with `and.b`. `byteclr` runs it (2026-11-18, qemu, 68030, 68040,
68060, every combination of callees and callers):

| Callees built by | `byteclr` | wrong results per run |
|---|---|---|
| GCC 14.2.0 unpatched (also fails `bitops`) | FAILED | 12 (both clears and the set, for each large `n`) |
| GCC 14.2.0 or GCC 16.2.0 with the bset/bclr patch only | FAILED | 8 (both clears, for each large `n`) |
| GCC 14.2.0 or GCC 16.2.0 with both patches | ok | 0 |

`muldiv64`, `structret`, `manyargs`, `inlineasm` and `loops` pass under all
five compilers, and `bitops` fails only with the unpatched one. Upstream has
not been told of the bit-field form or the guard.
