# Running the shared-library tests on the machine

Five scripts build shared libraries and programs, check them on the host with
`readelf` and `nm`, and stage what the host cannot check in a directory under
`build/`:

| Script | Staged in | Run there |
|---|---|---|
| `tools/test-shared.sh` | `build/test-shared/` | `sh run-shared.sh` |
| `tools/test-shared-indirect.sh` | `build/test-shared-indirect/` | `sh run-shared-indirect.sh` |
| `tools/test-pic-globals.sh` | `build/test-pic-globals/` | `sh run-pic-globals.sh` |
| `tools/test-amixrt.sh` | `build/test-amixrt/` | `sh run-amixrt.sh`, then `sh ram-measure.sh` as root |
| `tools/test-cxx-shared.sh` | `build/test-cxx-shared/` | `sh run-cxx-shared.sh` |

Each directory is complete: the libraries, the programs, `libamixrt.so.1`
and `libstdc++.so.6` where they need them, and a script that sets `LD_LIBRARY_PATH` to the directory
itself. Copy a directory to the machine (FTP in binary mode, or
`tools/push-run.sh`), `cd` into it and run its script. Run every one on a
68030 with a 68881 or 68882 and on a 68060 on a kernel of the 040/060 port:
AMIX's 68030 kernels map memory in 2 KiB pages, the port's 68060 kernel in
4 KiB, and a fault can show on one and not the other. Nothing here needs root
except `ram-measure.sh`, which reads kernel memory.

**Set the machine's date first.** The clock checks in
`run-shared-indirect.sh` and `run-amixrt.sh` call a date before 2020-01-01
wrong, and an emulated machine's clock often starts in 1992. Each probe prints
the date it read, so a wrong clock is easy to tell from a wrong
`gettimeofday`.

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
`solib-dep ready=1`, then `constructor order: solib-dep before solib-mid`,
the date the machine's clock reads, a clock line ending in `PASS`, and
`exit status: 0`.

- `can't find libsolibdep.so.1` or `libamixrt.so.1`: the runtime linker did
  not follow a library's `DT_NEEDED`.
- `solib-mid before solib-dep`: the deferred constructors did not run last in,
  first out, or the runtime linker's `DT_INIT` order is not the one read off
  `_rtld`.
- `WRONG` on the clock with a date after 2020 printed: `libamixrt`'s
  `gettimeofday` does not speak `hrtcntl` as libc's does.

**`run-pic-globals.sh`: a library's pointer to its own exported data.** One
line with `(same)`, ending in `PASS`, then `exit status: 0`. `DIFFERENT` means
the library still points at its own copy of the record and not at the
program's `R_68K_COPY` of it: the binutils patch is missing from the
toolchain that built it.

**`run-amixrt.sh`: `libamixrt` itself, state, `dlopen`, libc's members.** Three
programs, a pair compared with each other, and a diagnostic:

- `solib-main-rt` calls every `libamixrt` function from a library reached
  only through another one, so the copies used are `libamixrt`'s. One line
  per check, every one `ok`, then `PASS`: `gettimeofday`; `select` sleeping
  200 ms and seeing a pipe become readable; `usleep`; `truncate` and
  `ftruncate`; `realpath` through a symbolic link, with a null buffer, and
  failing with `ENOENT`; a repeating interval timer that keeps its period,
  fires at least twice in four seconds and disarms, and a one-shot that fires
  once; the message tables matching `strerror`; `snprintf` with `%lld`;
  `sysinfo` giving `uname`'s system and node names, and failing with
  `EINVAL` for an unknown command. Before `libamixrt` set `errno` itself, that
  last line read `FAIL  -1, errno -1056833824` on k30 and k46: `errno` held
  `_cerror`'s address, `0xC101FEE0` (the libamixrt rules in
  `docs/TOOLCHAIN.md`). It takes about ten seconds.
- `solib-main-interpose` sets `drand48`'s seed, the syslog mask and an
  interval timer in the program and reads them in a library, and the other
  way round. Every line `ok`, then `PASS`. A failure on the timer lines means
  the library reached `libamixrt`'s copy instead of the program's. It calls
  `openlog` before `setlogmask`: libc's `setlogmask` does not check for the
  state `openlog` allocates and follows a null pointer without it.
- `solib-dlopen` opens `libsolibmid.so.1` with `dlopen`; the runtime linker
  has to load `libsolibdep.so.1` and `libamixrt.so.1` behind it. Expected:
  `solib-dep`'s constructor lines, then `solib-mid`'s with
  `solib-dep ready=1`, both with `(stdio)`; then
  `constructor order: solib-dep before solib-mid`, the clock line and `PASS`.
  `solib-mid before solib-dep` means `__wrap_dlopen` did not order them: the
  program was not linked by these specs, or does not export
  `__amix_dso_batch`.
- `libc-static` and `libc-amixrt` run the same calls, `probe/libc-calls.c`:
  `libc-static` with libc's own static members, `libc-amixrt` through a
  library it reaches only through another one, so with `libamixrt`'s second
  wave. The script prints `libc-amixrt`'s output and ends the section with
  `libc-calls: libamixrt's output is libc's: PASS`; a difference prints
  `FAIL` and the `diff`, libc's lines marked `<`. Both exit with status 0.
  Run as an ordinary user and as root: the scratch files go in `/tmp`.
  What the output shows, and what libc's members give:
  - `siglongjmp`: `sigsetjmp returned 0`, then `1` (for 0) and `42`, with
    `locals 11 22` each time, then `SIGUSR1 blocked after the jumps: yes`:
    `libc.so.1`'s `sigsetjmp` never saves the mask, so neither `siglongjmp`
    restores it.
  - `vfork`: `child exit 7, its store seen by the parent 1, local 5`; the 1
    says the child ran in the parent's memory.
  - `_fpsetround` and `_fpsetsticky`, from FPCR and FPSR values the probe
    loads first: the new FPCR has the mode in bits 4-5 (`_fpsetround(2)`
    from 0 gives FPCR `00000020`), `__flt_rounds` reads 0, 1, 3, 2 for modes
    0 to 3, and the results are the old register with the field cleared
    (shifted right 4 for the FPCR), not the old mode or flags. The same
    comparison runs under qemu-m68k on the host, so a difference here points
    at the FPU or its emulation.
  - `syslog`: the messages go to a `putmsg` the programs define, not to the
    log. `setlogmask returned 0xff, then 0x7f`; a line per message sent,
    `ctl len 24 maxlen 24, level 0, flags 0x10`, the priority with the
    openlog facility added (134, `LOCAL0|INFO`) or the message's own (19,
    `MAIL|ERR`), `maxlen 1025`, and the text after the time:
    `amixrt-probe[<pid>]: probe message 7: No such file or directory, 100%`,
    cut at the format's newline. `LOG_DEBUG`, priority 0 and facility 24 are
    not sent; after `closelog` the next message reopens the device; after
    `openlog(0, 0, 0)` the ident stays and the pid goes.
  - `getmntent`: a scratch file with comments, `-` fields (`(null)`), too
    few fields (3), too many (2), a line of 1098 characters (1, then 3 for
    its rest), and `getmntany` stopping at the first bad line (3); then the
    machine's `/etc/mnttab`, every line 0, and `getmntany` finding `/`.
  - The utmpx group, on a scratch pair `/tmp/amixrt-lc.<pid>.utmpx` and
    `.utmp`: `utmpxname` gives `0, 0, 1`; new files, both created empty,
    `mode 644` under umask 022; four `pututxline` calls leaving three
    records in each file, `p1` overwritten in place as `type 8 ... exit
    1/2`; the searches (`getutxline ttyp2` twice the same entry, `ttyp1`
    NULL once dead, `getutxid p1` the dead entry, `boot` the boot record,
    `empty` and `type 12` NULL); `3 entries`; utmpx made from a utmp with an
    empty utmpx (`pid -2`, `syslen 0`); utmp made from a utmpx 100 s newer
    (three records, the last `user newer`); files 9 s apart left alone
    (utmp 4 records); and a utmp of part records failing with errno 22.
    The byte sums are of whole records, so any byte that differs from libc's
    shows.
- `nullread` is a diagnostic of the kernel, not a test: it says whether
  reading through a null pointer faults. Where page zero is mapped, a null
  dereference carries on silently, so a program can pass on that machine and
  crash on another; report what it prints with the rest.

Each program ends with `exit status: 0`.

**`run-cxx-shared.sh`: the shared C++ runtime.** Three programs:

- `cxxso-main`, linked with `g++ -shared-libgcc`: first
  `libcxxsoa: static object constructed (std::cout)`, before anything of
  `main`, then nine numbered lines, each ending in `ok`, then `PASS` and
  `exit status: 0`. The steps: libcxxsoa's constructor ran; virtual calls
  into both libraries; `dynamic_cast` to a type of libcxxsob; an exception
  thrown in libcxxsoa passing through libcxxsob and caught in the program;
  one thrown in libcxxsoa and caught in libcxxsob; one thrown in the program
  through libcxxsob and caught by its base class; `std::vector::at` throwing
  from `libstdc++.so.6`; a rethrow caught as `std::runtime_error`; a
  `what()` string built in libcxxsoa.
- `cxxso-cmain`, a C program linked with plain gcc: the libcxxsoa
  constructor line, then
  `cxxso-cmain: b_c_api(5)=25 (25) b_c_api(11)=-121 (-121) -> PASS` and
  `exit status: 0`.
- `cxxso-main-static`, the same program linked the default way, with a
  C++ runtime of its own: no line from `main`, the message
  `libstdc++.so.6: this program has a C++ runtime of its own ...` on
  standard error, and `exit status: 127`.

What a failure means:

- The `std::cout` line missing, or a crash before step 1:
  `libstdc++.so.6`'s constructors did not run before libcxxsoa's.
- A step that dies with `terminate called` or `abort`: the unwinder does
  not know the `.eh_frame` of an object on the stack, so the library or the
  program did not register with `libstdc++.so.6`'s copy.
- Step 3 or 4 `FAIL` without a crash: the objects do not agree on one
  `type_info`.
- `cxxso-main` stopping with the message, or `cxxso-main-static` running
  `main`: the start-up check is wrong in one direction or the other.
- `can't find libstdc++.so.6`: it was not copied along, or
  `LD_LIBRARY_PATH` was not set.

**`ram-measure.sh`: what `libamixrt.so.1` costs a process.** As root:
`sh ram-measure.sh 10`. It starts ten copies of `ram-plain`, a program whose
one library NEEDs nothing, then ten of `ram-amixrt`, the same program whose
library NEEDs `libamixrt.so.1`, and prints the kernel's `freemem` before each
batch and while it runs, and `ps -l` for the first few. It reads `freemem`
with `echo "freemem/D" | adb -k /stand/unix /dev/kmem`, so the kernel booted
has to be the one in `/stand/unix`, as with any 1.1 module kernel; with
another, `adb` reads the wrong symbol table and the figures mean nothing.
`freemem` counts pages: 2 KiB on a 68030, 4 KiB on a 68060. Per process, a
batch costs (before - running) / 10 pages; the difference between the two
batches is `libamixrt`'s share, its data segment, since its text is shared by
all of them. Measured on a 68030 with a 1.1 kernel: 2 pages of 2 KiB, 4 KiB.
The `SZ` column of `ps -l` should differ by about the same. Run it twice and
take the second figures; the first batch also pulls the text of the libraries
into memory.

## When something fails

Keep the whole output of the run. For a crash, the last line printed names
the step: every probe flushes after each line. The host checks run again with
the script that staged the directory; a probe that passes there and fails on
the machine is a question only the machine can answer, which is why it is
staged.
