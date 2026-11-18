# Regression cases for `tools/check-bset-mem.py`

Small assembly files, one per way the scan has gone wrong on real binaries.
Run them with

    tools/test-check-bset-mem.sh [path/to/check-bset-mem.py]

Each file is assembled with the GCC 14 binutils, and the scan is run on the
object. The file's own `| expect:` comment gives the exit status, the number
of sites and, where it matters, the functions. Most cases have one real
`bset %d1,(%a0)` with an unknown bit number after a switch table. That site
must be flagged, and the table's words must not be: they decode as
`bset`/`bclr %dN,%a0@`.

| Case | What went wrong before |
|---|---|
| `swbeg-4afc.s` | a 4-byte count prefix `4afc N` between the `jmp` and its table (the SVR4 compiler's swbeg, stock libc): the table was decoded as code |
| `swbeg-0000.s` | the same with `0000 N` (GCC 2.7.2.3) |
| `swbeg-stripped.s` | the swbeg case linked with no symbols, as the k30 kernel is: objdump prints the table base as `%pc@(0x...,%d0:w)`, which was not recognised |
| `bound-cmpa.s` | the bound `cmpa #N,%aX` was not read, and a label between table and cases ended the table |
| `bound-cmpib-negative.s` | `cmpi.b #-4` was taken as -3 entries; read unsigned at byte size, it is 253 |
| `bound-lower-compare.s` | a signed lower-bound compare (`bge`) was taken as the bound |
| `index-fp.s` | a switch indexed by `%fp` was not recognised |
| `section-null-name.s` | linked, then its code section is given string-table offset 0 as its name, as the k30 kernel's is: objdump prints `Disassembly of section :`, and the scan decoded nothing and passed |
| `no-code.s` | a file with no code passed; now exit 2, or a pass with `--no-code-ok` |
| `masks.s` | a bit number masked with `andi.b #7` (with or without a later `andi.l #255`), or produced by a 3-bit `bfextu`, was flagged; `andi.b #15` must still be |

With the scan as it was at `c9eb0f4`, every case fails.
