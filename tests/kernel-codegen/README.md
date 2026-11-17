# Kernel code-generation test set

Small freestanding cases that compare what two compilers make of the kernel's
kind of C: GCC 14 (`toolchain/build-cross.sh`) and GCC 16
(`toolchain/build-gcc16.sh`). Run it with

    tools/test-kernel-codegen.sh

Every case is compiled by each compiler with the same flags (`-O2 -m68020-60
-msoft-float -ffreestanding -fno-common -std=gnu99`; set `CFLAGS` to change
them), disassembled, and diffed between the compilers. The cases that can run
are then linked four ways -- each compiler's callees with each compiler's
callers -- and run under `qemu-m68k` user emulation on a 68030, a 68040 and a
68060, which checks results and the calling convention across the two
compilers at once. Output goes to `build/gcc16/codegen/`.

| File | What it holds | Run |
|---|---|---|
| `crt0.s`, `rt.c`, `rt.h` | entry point and output through Linux m68k system calls, for qemu only | -- |
| `muldiv64.c`, `muldiv64_check.c` | 64-bit multiply, divide and remainder by variables and by constants, 32x32->64 widening multiply, 32-bit division by constants | yes |
| `structret.c`, `structret_check.c` | struct returns of 1 to 16 bytes, a pointer return | yes |
| `manyargs.c`, `manyargs_check.c` | a function of twelve mixed arguments, a variadic one | yes |
| `inlineasm.c`, `inlineasm_check.c` | user-mode inline asm with register, tied, memory and address operands | yes |
| `bitops.c`, `bitops_check.c` | `bset`/`bclr` on memory with a variable bit number (GCC PR 126121) | yes |
| `loops.c`, `loops_check.c` | counted loops the back end turns into `dbra`, with 32-bit counters | yes |
| `kasm.c` | supervisor inline asm (`movec`, `move sr`, `moves`), volatile device-register access | compile only |
| `sup030.s`, `sup040.s`, `sup060.s` | supervisor instructions, assembled with `-m68030`, `-m68040`, `-m68060` | assemble only |

qemu's 68060 model traps the 64-bit `mulu.l`/`divu.l` forms the real 68060
lacks (measured with qemu 10.2.1: `divu.l %d2,%d0:%d1` runs on its 68030 and
68040 and is an illegal instruction on its 68060), so the 68060 run also
covers the libgcc helpers the cases call. The C objects are in addition
checked from their encodings with `tools/check-68060-safe.py`.
