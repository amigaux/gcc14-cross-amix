#!/usr/bin/env python3
"""Find code that GCC PR target/126121 makes set or clear the wrong bit.

THE FAULT
m68k BSET and BCLR with a data register as the bit number and a memory
operand use only the low three bits of the register: `bset %d0,(%a0)` with
%d0 = 10 sets bit 2 of the byte.  Unpatched GCC (14.2.0, 16.2.0) modelled the
full number, so for C whose result is defined for any shift below 32 --

    *p |= (unsigned char) (1 << n);      n = 10: (char) 0x400 is 0, no change
    *p &= ~(1 << n);                     n = 10: the low byte is 0xff, no change

-- it emitted a bare `bset`/`bclr %dN,<mem>` that changes bit n & 7 instead.
The fix (toolchain/patches/gcc-bset-bclr-mem-bit-number.patch, upstream GCC
commit b407be9e4b) makes the patterns say "bit number & 7", so GCC uses
them only when the number is known to be below 8 -- after an explicit `& 7`
in the source, which it then leaves out because the instruction does it.

ONE FORM THE FIX DOES NOT COVER
With the fix, `*p &= ~(1 << n)` on a byte no longer matches BCLR and falls
to the bit-field instruction instead: `moveq #7,%d0; sub.l n,%d0;
bfclr (%a0){%d0:#1}`.  BFCLR takes a signed bit offset that may leave the
byte, so with n = 10 the offset is -3 and it clears bit 2 of the byte BEFORE
*p -- a write outside the object (measured under qemu on a 68030, 68040
and 68060 with GCC 14.2.0 and GCC 16.2.0, both with the fix; see
docs/TOOLCHAIN.md, "GCC carries one patch").  The middle end builds that
bit-field RTL at position 7 - n without bounding n; the m68k fix only stops
BCLR from standing in for it.  This tool flags that form as well.

WHAT IS FLAGGED
  bset/bclr  %dN,<memory>             %dN not shown to be 0..7
  bf{clr,set,chg,ins,tst,extu,exts,ffo} <memory>{%dN:...}
                                      %dN computed as 7 - x, x not shown 0..7
  btst       %dN,<memory>             %dN not shown to be 0..7
BTST uses the same low three bits, and GCC's patterns for it on memory model
the full number the way BSET/BCLR's did.  No case here makes GCC emit it, but
it is the same disagreement between the CPU and the compiler, so it is
reported the same way.

"Shown" is a small forward walk over each basic block: a register's value is
known after moveq/move.l #k, clr.l, and.l with a non-negative constant or a
register holding one, move.l from a known register, and add/sub of known
values.  A label, a branch target, the start of a function or a call ends what
is known.  So a finding is a place the scan could not prove safe, not proof
of a fault: a number masked with & 7 in another block is not seen as small.
A site that has been read and found safe can be named with --allow FUNCTION.

Input: GCC assembly output (gcc -S, Motorola syntax as GCC prints it),
objdump -d listings, or ELF objects, archives, executables and shared
libraries (disassembled here with an m68k objdump: --objdump, $OBJDUMP,
config.sh's TC_PREFIX, then m68k-elf-objdump on PATH).  A directory is
searched for *.s, *.o, *.a, *.so* and *.dis.

objdump decodes GCC's switch tables, which sit in .text right after their
`jmp %pc@(...)`, as instructions: in libstdc++ that alone made six bogus
findings.  A table is skipped here -- its length from the bounds check
before the jmp, or up to the first word that is not an even address in the
function, or the first case it reaches -- and when objdump ran the table's
last word together with the first real instruction, the code after it is
decoded again from there (objdump --start-address), so nothing real is
swallowed.  An objdump listing given as input cannot be decoded again; a
table that loses step there is reported as a finding.

Usage: check-bset-mem.py [--objdump PATH] [--allow FUNC]... [-q] FILE|DIR ...
Exit status 0 if nothing is flagged, 1 if anything is, 2 if an input could
not be read.
"""
import os
import re
import subprocess
import sys

PROG = "check-bset-mem.py"


def die(msg):
    sys.stderr.write("%s: %s\n" % (PROG, msg))
    sys.exit(2)


# ------------------------------------------------------------- disassembly --
def objdump_candidates(explicit):
    names = []
    if explicit:
        return [explicit]
    if os.environ.get("OBJDUMP"):
        names.append(os.environ["OBJDUMP"])
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cfg = os.path.join(here, "config.sh")
    prefix = os.environ.get("TC_PREFIX")
    target = "m68k-elf"
    if os.path.exists(cfg):
        for line in open(cfg):
            line = line.strip()
            m = re.match(r'TC_PREFIX="?(?:\$\{TC_PREFIX:-)?([^"}]*)\}?"?$', line)
            if m and not prefix:
                prefix = os.path.expandvars(m.group(1))
            elif line.startswith("TC_TARGET="):
                target = line.split("=", 1)[1].strip().strip('"')
    if prefix:
        names.append(os.path.join(prefix, "bin", "%s-objdump" % target))
    names.append(os.path.join(here, "build", "prefix", "bin", "m68k-elf-objdump"))
    names += ["m68k-elf-objdump", "m68k-linux-gnu-objdump"]
    return names


def disassemble(path, explicit, extra=()):
    """objdump -d of path, and the objdump that read it."""
    tried = [explicit] if explicit and not isinstance(explicit, bool) else objdump_candidates(explicit)
    for objdump in tried:
        try:
            out = subprocess.check_output([objdump, "-d"] + list(extra) + [path],
                                          stderr=subprocess.DEVNULL).decode("latin1")
        except (OSError, subprocess.CalledProcessError):
            continue
        if "elf32-m68k" in out:
            return out, objdump
    die("%s: no objdump here could disassemble it for m68k (tried %s)"
        % (path, ", ".join(tried)))


# ------------------------------------------------------------- parsing ------
# objdump line: "  7a:\tecd0 0801      \tbfclr %a0@,%d0,1"
OBJ_INSN = re.compile(r"^\s*([0-9a-f]+):\t((?:[0-9a-f]{4} ?)+)\s*\t(\S+)\s*(.*)$")
# GCC's switch: "jmp %pc@(d22 <f+0x54>,%d0:w)" followed by a table of 16-bit
# offsets from d22 in .text, which objdump decodes as instructions.
OBJ_SWITCH = re.compile(r"^%pc@\(([0-9a-f]+)\b[^,]*,%[ad][0-7]:[wl]\)$")
OBJ_LABEL = re.compile(r"^([0-9a-f]+) <([^>]+)>:")
OBJ_MEMBER = re.compile(r"^(\S+):\s+file format ")
OBJ_SECTION = re.compile(r"^Disassembly of section (\S+):")
OBJ_TARGET = re.compile(r"^([0-9a-f]+) <")
# GCC assembly: "\tbset %d0,(%a0)", "name:", ".L3:"
ASM_LABEL = re.compile(r"^([A-Za-z_.$][\w.$]*):")
ASM_INSN = re.compile(r"^\s+([a-z][a-z0-9.]*)\s*(.*)$")

DREG = re.compile(r"^%d([0-7])$")
DPAIR = re.compile(r"^%d([0-7]):%d([0-7])$")
IMM = re.compile(r"^#(-?(?:0x[0-9a-fA-F]+|\d+))$")

# Size-suffixed mnemonics in objdump's MIT syntax drop the dot: andl, subql.
BASES = ("and", "andi", "or", "ori", "eor", "eori", "sub", "subi", "subq",
         "suba", "add", "addi", "addq", "adda", "move", "movea", "moveq",
         "clr", "neg", "not", "ext", "extb", "lsl", "lsr", "asl", "asr", "rol",
         "ror", "roxl", "roxr", "cmp", "cmpi", "cmpa", "tst", "muls", "mulu",
         "divs", "divu", "divsl", "divul", "movem", "exg", "swap", "chk",
         "mvs", "mvz", "negx", "addx", "subx")
READ_ONLY = {"cmp", "cmpi", "cmpa", "tst", "btst", "chk", "pea", "jmp", "jsr",
             "bsr", "jbsr", "tas"}
BF = {"bfclr", "bfset", "bfchg", "bfins", "bftst", "bfextu", "bfexts", "bfffo"}


def split_ops(text):
    text = text.split("|", 1)[0].strip()   # GCC asm comment
    ops, depth, cur = [], 0, ""
    for ch in text:
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
        if ch == "," and depth == 0:
            ops.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        ops.append(cur.strip())
    return ops


def norm(mn):
    """Mnemonic without size: and.l, andl, andi.l, andil -> and/andi."""
    mn = mn.lower()
    size = ""
    if "." in mn:
        mn, size = mn.split(".", 1)
    elif mn not in BASES and mn[:-1] in BASES and mn[-1] in "bwls":
        mn, size = mn[:-1], mn[-1]
    return mn, size


def is_branch(mn):
    if mn in ("rts", "rte", "rtr", "rtd", "jmp", "bra", "jra", "jbra", "trap",
              "illegal", "stop"):
        return "end"
    if mn in ("jsr", "bsr", "jbsr"):
        return "call"
    if re.match(r"^(j|b)(cc|cs|eq|ne|ge|gt|le|lt|hi|ls|pl|mi|vc|vs|hs|lo)[bswl]?$", mn) \
            or re.match(r"^jb?(cc|cs|eq|ne|ge|gt|le|lt|hi|ls|pl|mi|vc|vs|hs|lo)$", mn) \
            or mn.startswith("db"):
        return "cond"
    if re.match(r"^bra[bswl]?$", mn) or re.match(r"^bsr[bswl]?$", mn):
        return "end" if mn.startswith("bra") else "call"
    return None


def imm(op):
    m = IMM.match(op)
    return int(m.group(1), 0) if m else None


# ------------------------------------------------------------- the walk -----
class Walker:
    """Known values of %d0-%d7 inside one basic block.  A value is
    (lo, hi), or the marker SEVEN_MINUS for "7 - x, x not known 0..7"."""
    SEVEN_MINUS = "7-x"

    def __init__(self):
        self.reset()

    def reset(self):
        self.r = {}

    def get(self, op):
        m = DREG.match(op)
        if m:
            return self.r.get(int(m.group(1)))
        k = imm(op)
        return (k, k) if k is not None else None

    def known_small(self, n):
        v = self.r.get(n)
        return isinstance(v, tuple) and 0 <= v[0] and v[1] <= 7

    def step(self, mn, size, ops):
        """Update the state for one instruction."""
        dests = set()
        if mn == "exg":
            for o in ops:
                m = DREG.match(o)
                if m:
                    dests.add(int(m.group(1)))
        elif mn == "movem":
            if ops and "%d" in ops[-1] and "@" not in ops[-1] and "(" not in ops[-1]:
                for a, b in re.findall(r"%d([0-7])(?:-%d([0-7]))?", ops[-1]):
                    for n in range(int(a), int(b or a) + 1):
                        dests.add(n)
        elif ops and mn not in READ_ONLY and not mn.startswith("bf"):
            last = ops[-1]
            m = DREG.match(last)
            if m:
                dests.add(int(m.group(1)))
            for pair in DPAIR.findall(last):
                dests.update(int(x) for x in pair)
        elif mn in ("bfextu", "bfexts", "bfffo") and ops:
            m = DREG.match(ops[-1])
            if m:
                dests.add(int(m.group(1)))
        if not dests:
            return
        new = None
        if len(dests) == 1:
            d = next(iter(dests))
            src = ops[0] if len(ops) == 2 else None
            old = self.r.get(d)
            sv = self.get(src) if src is not None else None
            long_op = size in ("l", "") and mn not in ("movea",)
            if mn == "moveq" and sv:
                new = sv
            elif mn == "move" and size == "l" and sv:
                new = sv
            elif mn == "clr" and size == "l":
                new = (0, 0)
            elif mn in ("and", "andi") and long_op and len(ops) == 2:
                bound = None
                for v in (sv, old):
                    if isinstance(v, tuple) and v[0] >= 0:
                        bound = v[1] if bound is None else min(bound, v[1])
                if bound is not None:
                    new = (0, bound)
            elif mn in ("sub", "subi", "subq") and long_op and len(ops) == 2:
                if isinstance(old, tuple) and isinstance(sv, tuple):
                    new = (old[0] - sv[1], old[1] - sv[0])
                elif old == (7, 7):
                    new = self.SEVEN_MINUS
            elif mn in ("add", "addi", "addq") and long_op and len(ops) == 2:
                if isinstance(old, tuple) and isinstance(sv, tuple):
                    new = (old[0] + sv[0], old[1] + sv[1])
        for d in dests:
            self.r.pop(d, None)
        if new is not None:
            self.r[next(iter(dests))] = new


REG = re.compile(r"^%(?:[ad][0-7]|sp|fp|pc|sr|ccr|usp)$")


def is_memory(op):
    """Neither a register nor an immediate: (%a0), 8(%sp), %a0@, %sp@(8)..."""
    return bool(op) and not REG.match(op) and not op.startswith("#")


def bf_parts(ops):
    """(memory operand, offset operand) of a bit-field instruction in either
    syntax: GCC's "(%a0){%d0:#1}" or objdump's "%a0@,%d0,1"."""
    for o in ops:
        m = re.match(r"^(.*)\{([^:]+):([^}]+)\}$", o)
        if m:
            return m.group(1), m.group(2)
    if len(ops) >= 3:
        # objdump: bfclr ea,off,width / bfins dn,ea,off,width / bfextu ea,off,width,dn
        if DREG.match(ops[0]) and len(ops) >= 4:
            return ops[1], ops[2]
        return ops[0], ops[1]
    return None, None


SEEN = {"reg-bit-on-memory": 0, "bitfield-on-memory": 0}


def check(mn, ops, w):
    """A reason string if this instruction is a finding."""
    if mn in ("bset", "bclr", "btst") and len(ops) == 2:
        m = DREG.match(ops[0])
        if m and is_memory(ops[1]):
            SEEN["reg-bit-on-memory"] += 1
        if m and is_memory(ops[1]) and not w.known_small(int(m.group(1))):
            return ("%s with the bit number in a register on memory: the CPU uses "
                    "%%d%s & 7, and nothing shows %%d%s is below 8" % (mn, m.group(1), m.group(1)))
    if mn in BF:
        mem, off = bf_parts(ops)
        if mem is not None and is_memory(mem):
            SEEN["bitfield-on-memory"] += 1
            m = DREG.match(off or "")
            if m and w.r.get(int(m.group(1))) == Walker.SEVEN_MINUS:
                return ("%s on memory at offset 7 - x with x not shown below 8: "
                        "for x >= 8 it reaches the byte before (PR 126121, the "
                        "form the m68k fix leaves)" % mn)
    return None


# ------------------------------------------------------------- the scan -----
def parse_listing(text):
    """objdump -d text as records: ("member", name), ("section", in_code, name),
    ("label", addr, name) and ("insn", addr, words, mnemonic, operands)."""
    recs = []
    for line in text.split("\n"):
        mm = OBJ_MEMBER.match(line)
        if mm:
            recs.append(("member", mm.group(1)))
            continue
        ms = OBJ_SECTION.match(line)
        if ms:
            name = ms.group(1).rstrip(":")
            recs.append(("section", name.startswith((".text", ".init", ".fini")), name))
            continue
        ml = OBJ_LABEL.match(line)
        if ml:
            recs.append(("label", int(ml.group(1), 16), ml.group(2)))
            continue
        m = OBJ_INSN.match(line)
        if m:
            recs.append(("insn", int(m.group(1), 16), m.group(2).split(), m.group(3), m.group(4)))
    return recs


def redecode(path, objdump, member, section, start, stop):
    """Instructions of one member's section from start to stop, decoded again
    from start: what objdump makes of the code after a switch table when it is
    not carried over from the table's last words.  (The addresses apply to
    every section of every member, hence the filter.)"""
    text, _ = disassemble(path, objdump, ["-j", section,
                                          "--start-address=0x%x" % start,
                                          "--stop-address=0x%x" % stop])
    cur, sec, got = "", "", []
    for r in parse_listing(text):
        if r[0] == "member":
            cur = r[1]
        elif r[0] == "section":
            sec = r[2]
        elif r[0] == "insn" and cur == member and sec == section and start <= r[1] < stop:
            got.append(r)
    return got


def function_end(recs, i):
    """Address of the next label in the same member and section."""
    for x in recs[i:]:
        if x[0] == "label":
            return x[1]
        if x[0] in ("member", "section"):
            break
    return float("inf")


def switch_entries(recs, j):
    """The number of entries of the switch table whose jmp is recs[j], from
    the bounds check GCC puts before it (cmpi #N,%dX, or moveq #N,%dY and a
    cmp against %dY, then a branch to the default): N + 1, or None."""
    moveq = {}
    seen = 0
    for k in range(j - 1, max(j - 12, -1), -1):
        r = recs[k]
        if r[0] != "insn":
            break
        mn, _ = norm(r[3])
        ops = split_ops(r[4])
        if mn == "cmpi" and len(ops) == 2 and imm(ops[0]) is not None:
            return imm(ops[0]) + 1
        if mn == "cmp" and len(ops) == 2:
            seen = ops
        if mn == "moveq" and len(ops) == 2 and seen and ops[1] in seen:
            return imm(ops[0]) + 1
    return None


def scan_objdump(path, text, allow, out, objdump=None):
    recs = parse_listing(text)
    # Branch targets per member, so a join in the middle ends what is known.
    targets, member = set(), ""
    for r in recs:
        if r[0] == "member":
            member = r[1]
        elif r[0] == "insn" and is_branch(norm(r[3])[0]) in ("cond", "end"):
            t = OBJ_TARGET.match(r[4].split(",")[-1].strip())
            if t:
                targets.add((member, int(t.group(1), 16)))
    func, member, section, in_code, table, fstart = "?", "", "", False, None, 0
    w = Walker()
    found = 0
    i = 0
    while i < len(recs):
        r = recs[i]
        i += 1
        if r[0] == "member":
            member, in_code, table = r[1], False, None
            continue
        if r[0] == "section":
            in_code, section, table = r[1], r[2], None
            w.reset()
            continue
        if r[0] == "label":
            func, fstart, table = r[2], r[1], None
            w.reset()
            continue
        if not in_code:
            continue
        _, addr, words, mnem, opstext = r
        if table is not None:
            # Inside a switch table: each word is an offset from its base.
            # It ends after the number of entries the bounds check before the
            # jmp allows, or else at the first word that is not an even
            # address inside the function, or at the first case it reaches.
            base, tend, nent, fstart, fend = table
            for k, word in enumerate(words):
                a = addr + 2 * k
                if a >= tend:
                    break
                if nent is not None and a >= base + 2 * nent:
                    tend = a
                    break
                off = int(word, 16)
                t = base + (off - 0x10000 if off & 0x8000 else off)
                if t & 1 or not fstart <= t < fend:
                    tend = a
                    break
                targets.add((member, t))
                if t > a:
                    tend = min(tend, t)
            if addr < tend and addr + 2 * len(words) <= tend:
                table = (base, tend, nent, fstart, fend)
                continue
            table = None
            if addr != tend:
                # objdump ran the table's last word into the first case and
                # lost step.  Decode again from the case, up to the point
                # where the two listings agree, and scan that instead.
                if objdump is None:
                    out.append("%s: 0x%x <%s>: the listing loses step after a switch "
                               "table; give the object file, not its listing" % (path, tend, func))
                    found += 1
                    continue
                stop = min(function_end(recs, i), tend + 0x10000)
                again = redecode(path, objdump, member, section, tend, stop)
                theirs = {x[1] for x in again}
                j = i - 1
                while j < len(recs) and recs[j][0] == "insn" and recs[j][1] not in theirs:
                    j += 1
                sync = recs[j][1] if j < len(recs) and recs[j][0] == "insn" else stop
                recs[i - 1:j] = [x for x in again if x[1] < sync]
                i -= 1
                continue
        if (member, addr) in targets:
            w.reset()
        mn, size = norm(mnem)
        ops = split_ops(opstext)
        why = check(mn, ops, w)
        if why and func not in allow:
            where = "%s%s" % (path, "(%s)" % os.path.basename(member) if member and member != path else "")
            out.append("%s: 0x%x <%s>: %s %s\n    %s" % (where, addr, func, mnem, opstext.strip(), why))
            found += 1
        if mn == "jmp" and len(ops) == 1:
            sw = OBJ_SWITCH.match(ops[0])
            if sw:
                fend = function_end(recs, i)
                table = (int(sw.group(1), 16), float("inf"), switch_entries(recs, i - 1),
                         fstart, fend)
        kind = is_branch(mn)
        if kind == "end":
            w.reset()
        elif kind == "call":
            for d in (0, 1):
                w.r.pop(d, None)
        else:
            w.step(mn, size, ops)
    return found


def scan_asm(path, text, allow, out):
    func = "?"
    w = Walker()
    found = 0
    for lineno, line in enumerate(text.split("\n"), 1):
        ml = ASM_LABEL.match(line)
        if ml:
            if not ml.group(1).startswith(".L"):
                func = ml.group(1)
            w.reset()
            continue
        m = ASM_INSN.match(line)
        if not m or m.group(1).startswith("."):
            continue
        mn, size = norm(m.group(1))
        ops = split_ops(m.group(2))
        why = check(mn, ops, w)
        if why and func not in allow:
            out.append("%s:%d: %s: %s %s\n    %s" % (path, lineno, func, m.group(1), m.group(2).strip(), why))
            found += 1
        kind = is_branch(mn)
        if kind == "end":
            w.reset()
        elif kind == "call":
            for d in (0, 1):
                w.r.pop(d, None)
        else:
            w.step(mn, size, ops)
    return found


SUFFIXES = (".s", ".o", ".a", ".dis")


def inputs(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                for f in sorted(files):
                    if f.endswith(SUFFIXES) or ".so" in f:
                        yield os.path.join(root, f)
        else:
            yield p


def main(argv):
    allow, paths, objdump, quiet = set(), [], None, False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--allow", "--objdump") and i + 1 < len(argv):
            if a == "--allow":
                allow.add(argv[i + 1])
            else:
                objdump = argv[i + 1]
            i += 2
            continue
        if a == "-q":
            quiet = True
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        elif a.startswith("-"):
            die("unknown option %s" % a)
        else:
            paths.append(a)
        i += 1
    if not paths:
        die("usage: %s [--objdump PATH] [--allow FUNC]... [-q] FILE|DIR ..." % PROG)
    out, found, files = [], 0, 0
    for path in inputs(paths):
        try:
            with open(path, "rb") as f:     # once: a pipe cannot be read twice
                data = f.read()
        except OSError as e:
            die("%s: %s" % (path, e.strerror))
        files += 1
        if data[:4] == b"\x7fELF" or data[:8] == b"!<arch>\n":
            text, used = disassemble(path, objdump)
            found += scan_objdump(path, text, allow, out, used)
            continue
        text = data.decode("latin1")
        if "Disassembly of section" in text:
            found += scan_objdump(path, text, allow, out)
        elif path.endswith(".s") or "\t.text" in text or "#NO_APP" in text:
            found += scan_asm(path, text, allow, out)
        else:
            die("%s is neither assembly, an objdump listing nor an ELF file -- "
                "refusing to report it clean" % path)
    if out and not quiet:
        print("\n".join(out))
    if not quiet:
        print("%s: seen %d bset/bclr/btst with a register bit number on memory, "
              "%d bit-field instructions on memory" % (PROG, SEEN["reg-bit-on-memory"],
                                                      SEEN["bitfield-on-memory"]))
    if found:
        print("%s: %d site(s) in %d file(s) where a register bit number on memory "
              "may not be what the C source meant (GCC PR 126121)" % (PROG, found, files))
        return 1
    if not quiet:
        print("%s: %d file(s), nothing flagged" % (PROG, files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
