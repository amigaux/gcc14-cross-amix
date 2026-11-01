# Running the shared-library tests on the machine

Four scripts build shared libraries and programs, check them on the host with
`readelf` and `nm`, and stage what the host cannot check in a directory under
`build/`:

| Script | Staged in | Run there |
|---|---|---|
| `tools/test-shared.sh` | `build/test-shared/` | `sh run-shared.sh` |
| `tools/test-shared-indirect.sh` | `build/test-shared-indirect/` | `sh run-shared-indirect.sh` |
| `tools/test-pic-globals.sh` | `build/test-pic-globals/` | `sh run-pic-globals.sh` |
| `tools/test-amixrt.sh` | `build/test-amixrt/` | `sh run-amixrt.sh`, then `sh ram-measure.sh` as root |

Each directory is complete: the libraries, the programs, `libamixrt.so.1`
where they need it, and a script that sets `LD_LIBRARY_PATH` to the directory
itself. Copy a directory to the machine (FTP in binary mode, or
`tools/push-run.sh`), `cd` into it and run its script. Run every one on two
machines: a 68030 with a 68881 or 68882 on the stock 2.1c kernel, and a
68060 on a kernel of the 040/060 port. Nothing here needs root except
`ram-measure.sh`, which reads kernel memory.

## What each run shows, and what to expect

**`run-shared.sh`: constructors, destructors, C++.** For each program: each
library constructor's two lines, `(write)` then `(stdio)`, before `main`'s
line; `main`'s line with `env=1,1` and ending in `PASS`; then the
destructors, the C++ ones first, newest first, then `solib-c`'s; and
`exit status: 0`.

- A `(write)` line without its `(stdio)` line, or `env=0`: the constructor
  ran from the runtime linker's `DT_INIT`, before libc was consistent with
  the program.
- No constructor line at all: `.init_array` was not walked.

**`run-shared-indirect.sh`: transitive loading and constructor order.** The
program NEEDs only `libsolibmid.so.1`; `libsolibdep.so.1` and
`libamixrt.so.1` are loaded because the libraries NEED them. Expected:
`solib-dep`'s two constructor lines, then `solib-mid`'s with
`solib-dep ready=1`, then `constructor order: solib-dep before solib-mid`, a
clock line ending in `PASS`, and `exit status: 0`.

- `can't find libsolibdep.so.1` or `libamixrt.so.1`: the runtime linker did
  not follow a library's `DT_NEEDED`.
- `solib-mid before solib-dep`: the deferred constructors did not run last in,
  first out, or the runtime linker's `DT_INIT` order is not the one read off
  `_rtld`.
- `WRONG` on the clock: `libamixrt`'s `gettimeofday` does not speak
  `hrtcntl` as libc's does.

**`run-pic-globals.sh`: a library's pointer to its own exported data.** One
line with `(same)`, ending in `PASS`, then `exit status: 0`. `DIFFERENT` means
the library still points at its own copy of the record and not at the
program's `R_68K_COPY` of it: the binutils patch is missing from the
toolchain that built it.

**`run-amixrt.sh`: `libamixrt` itself, state, `dlopen`.** Three programs:

- `solib-main-rt` calls every `libamixrt` function from a library reached
  only through another one, so the copies used are `libamixrt`'s. One line
  per check, every one `ok`, then `PASS`: `gettimeofday`; `select` sleeping
  200 ms and seeing a pipe become readable; `usleep`; `truncate` and
  `ftruncate`; `realpath` through a symbolic link, with a null buffer, and
  failing with `ENOENT`; a repeating interval timer that keeps its period,
  fires at least twice in four seconds and disarms, and a one-shot that fires
  once; the message tables matching `strerror`; `snprintf` with `%lld`. It
  takes about ten seconds.
- `solib-main-interpose` sets `drand48`'s seed, the syslog mask and an
  interval timer in the program and reads them in a library, and the other
  way round. Every line `ok`, then `PASS`. A failure on the timer lines means
  the library reached `libamixrt`'s copy instead of the program's.
- `solib-dlopen` opens `libsolibmid.so.1` with `dlopen`; the runtime linker
  has to load `libsolibdep.so.1` and `libamixrt.so.1` behind it. Expected:
  both constructors' lines, both with `(stdio)`, then a clock line ending in
  `PASS`. The order line is reported, not judged: within one `dlopen` the
  runtime linker decides it.

Each program ends with `exit status: 0`.

**`ram-measure.sh`: what `libamixrt.so.1` costs a process.** As root:
`sh ram-measure.sh 10`. It starts ten copies of `ram-plain`, a program whose
one library NEEDs nothing, then ten of `ram-amixrt`, the same program whose
library NEEDs `libamixrt.so.1`, and prints the kernel's `freemem` (pages of
4 KB) before each batch and while it runs, and `ps -l` for the first few. It
reads `freemem` with `echo "freemem/D" | adb -k /stand/unix /dev/kmem`; if
the kernel booted is not `/stand/unix`, edit that line. Per process, a batch
costs (before - running) / 10 pages; the difference between the two batches
is `libamixrt`'s share, expected to be about one page, its data segment,
since its text is shared by all of them. The `SZ` column of `ps -l` should
differ by about the same. Run it twice and take the second figures; the
first batch also pulls the text of the libraries into memory.

## When something fails

Keep the whole output of the run. For a crash, the last line printed names
the step: every probe flushes after each line. The host checks run again with
the script that staged the directory; a probe that passes there and fails on
the machine is a question only the machine can answer, which is why it is
staged.
