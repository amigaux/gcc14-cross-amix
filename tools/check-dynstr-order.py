#!/usr/bin/env python3
"""check-dynstr-order.py -- are a dynamic ELF file's library names first in
its .dynstr?

WHY
---
AMIX's runtime linker is /usr/lib/libc.so.1.  When dlopen opens a library,
dl_makelist builds the list of objects the handle searches, following each
listed object's DT_NEEDED entries.  It takes each NEEDED offset from the
object's own dynamic section but looks it up in the string table of the
FIRST object of the list (libc.so.1 text 0xfcde-0xfcea: the table base comes
from the first link map, the offset from the current dynamic entry).  AMIX's
1991 ld writes the names of libraries at the start of .dynstr, so the
offsets are small and the read stays inside the first table.  GNU ld writes
them after every symbol name, so an offset can run past the first object's
table and the lookup reads whatever lies beyond it.  This toolchain's ld
puts them first (toolchain/patches/binutils-dynstr-needed-first.patch); this
script checks the result, on any ELF, whoever linked it.

THE PROPERTY
------------
Every string named by DT_NEEDED, DT_SONAME, DT_RPATH or DT_RUNPATH (and the
rarer DT_FILTER, DT_AUXILIARY, DT_AUDIT, DT_DEPAUDIT) lies before every other
string the dynamic section refers to: dynamic symbol names, version names.
A symbol name may still share bytes with a library name (ld merges a string
that is the tail of another); only a string that starts outside the name
block and before one of the names counts against it.  Every offset must also
lie inside DT_STRSZ.

The file is read through its program headers (PT_DYNAMIC, PT_LOAD), as the
runtime linker reads it, so a stripped file with no section headers is
checked the same way.  The symbol count comes from DT_HASH, or from the
.dynsym section when there is no SysV hash table.

Usage:
    check-dynstr-order.py [-v] FILE...

Exit status: 0 when every file has the property or is not dynamic (static
programs and relocatable objects pass: they have no .dynstr); 1 when any file
lacks it, with the offending entries listed; 2 when a file cannot be read
as ELF.
"""
import struct
import sys

PT_LOAD, PT_DYNAMIC = 1, 2
SHT_DYNSYM = 11

DT_NULL, DT_NEEDED, DT_HASH, DT_STRTAB, DT_SYMTAB = 0, 1, 4, 5, 6
DT_STRSZ, DT_SYMENT, DT_SONAME, DT_RPATH, DT_RUNPATH = 10, 11, 14, 15, 29
DT_VERDEF, DT_VERDEFNUM = 0x6FFFFFFC, 0x6FFFFFFD
DT_VERNEED, DT_VERNEEDNUM = 0x6FFFFFFE, 0x6FFFFFFF
DT_RELA, DT_REL, DT_JMPREL, DT_VERSYM = 7, 17, 23, 0x6FFFFFF0
TABLE_TAGS = (DT_HASH, DT_STRTAB, DT_RELA, DT_REL, DT_JMPREL, DT_VERSYM,
              DT_VERDEF, DT_VERNEED)
NAME_TAGS = {
    DT_NEEDED: "NEEDED", DT_SONAME: "SONAME", DT_RPATH: "RPATH",
    DT_RUNPATH: "RUNPATH", 0x7FFFFFFF: "FILTER", 0x7FFFFFFD: "AUXILIARY",
    0x6FFFFEFC: "AUDIT", 0x6FFFFEFB: "DEPAUDIT",
}


class NotElf(Exception):
    pass


class Elf:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        d = self.d
        if d[:4] != b"\x7fELF":
            raise NotElf("not an ELF file")
        if d[4] not in (1, 2) or d[5] not in (1, 2):
            raise NotElf("unknown ELF class or byte order")
        self.is64 = d[4] == 2
        self.end = ">" if d[5] == 2 else "<"
        if self.is64:
            (self.type, _, _, _, self.phoff, self.shoff, _, _, self.phentsize,
             self.phnum, self.shentsize, self.shnum, _) = self.u("HHIQQQIHHHHHH", 16)
        else:
            (self.type, _, _, _, self.phoff, self.shoff, _, _, self.phentsize,
             self.phnum, self.shentsize, self.shnum, _) = self.u("HHIIIIIHHHHHH", 16)
        self.phdrs = []
        for i in range(self.phnum):
            o = self.phoff + i * self.phentsize
            if self.is64:
                p_type, _, p_offset, p_vaddr, _, p_filesz, _, _ = self.u("IIQQQQQQ", o)
            else:
                p_type, p_offset, p_vaddr, _, p_filesz, _, _, _ = self.u("IIIIIIII", o)
            self.phdrs.append((p_type, p_offset, p_vaddr, p_filesz))

    def u(self, fmt, off):
        fmt = self.end + fmt
        if off < 0 or off + struct.calcsize(fmt) > len(self.d):
            raise NotElf("truncated at file offset %#x" % off)
        return struct.unpack_from(fmt, self.d, off)

    def word(self):
        return "Q" if self.is64 else "I"

    def file_offset(self, vaddr):
        """The file offset of an address in a PT_LOAD segment, or None."""
        for p_type, p_offset, p_vaddr, p_filesz in self.phdrs:
            if p_type == PT_LOAD and p_vaddr <= vaddr < p_vaddr + p_filesz:
                return p_offset + (vaddr - p_vaddr)
        return None

    def dynamic(self):
        """The dynamic entries as (tag, value), or None for a file without."""
        for p_type, p_offset, _, p_filesz in self.phdrs:
            if p_type == PT_DYNAMIC:
                break
        else:
            return None
        fmt = "qQ" if self.is64 else "iI"
        size = struct.calcsize(fmt)
        out = []
        for o in range(p_offset, p_offset + p_filesz - size + 1, size):
            tag, val = self.u(fmt, o)
            if tag == DT_NULL:
                break
            out.append((tag, val))
        return out

    def dynsym_count_from_sections(self):
        for i in range(self.shnum if self.shoff else 0):
            o = self.shoff + i * self.shentsize
            if self.is64:
                _, sh_type, _, _, _, sh_size, _, _, _, sh_entsize = self.u("IIQQQQIIQQ", o)
            else:
                _, sh_type, _, _, _, sh_size, _, _, _, sh_entsize = self.u("IIIIIIIIII", o)
            if sh_type == SHT_DYNSYM and sh_entsize:
                return sh_size // sh_entsize
        return None


def references(elf, dyn):
    """Every .dynstr offset the file refers to, as (offset, what, is_name)."""
    tags = {}
    for tag, val in dyn:
        tags.setdefault(tag, []).append(val)
    refs = []
    for tag, val in dyn:
        if tag in NAME_TAGS:
            refs.append((val, NAME_TAGS[tag], True))

    def at(tag):
        v = tags.get(tag)
        if not v:
            return None
        off = elf.file_offset(v[0])
        if off is None:
            raise NotElf("DT tag %#x points outside every PT_LOAD segment" % tag)
        return off

    # Dynamic symbols.  DT_HASH's nchain is the symbol count.
    symtab = at(DT_SYMTAB)
    if symtab is not None:
        hash_off = at(DT_HASH)
        if hash_off is not None:
            nsyms = elf.u("II", hash_off)[1]
        else:
            nsyms = elf.dynsym_count_from_sections()
            if nsyms is None:
                raise NotElf("DT_SYMTAB without DT_HASH or a .dynsym section: "
                             "cannot count the symbols")
        entsize = tags.get(DT_SYMENT, [24 if elf.is64 else 16])[0]
        # The symbol table ends where the next table starts.  AMIX's own ld
        # writes an nchain one larger than its symbol table (libc.so.1:
        # 1855 for 1854 symbols), and the extra entry is the start of
        # .dynstr read as a symbol.
        base = tags[DT_SYMTAB][0]
        following = [v[0] for t, v in tags.items()
                     if t in TABLE_TAGS and v[0] > base]
        if following:
            nsyms = min(nsyms, (min(following) - base) // entsize)
        for i in range(1, nsyms):
            st_name = elf.u("I", symtab + i * entsize)[0]
            if st_name:
                refs.append((st_name, "symbol #%d" % i, False))

    # Version definitions: Verdef {vd_version, vd_flags, vd_ndx, vd_cnt (H),
    # vd_hash, vd_aux, vd_next (I)}, Verdaux {vda_name, vda_next}.
    off = at(DT_VERDEF)
    for _ in range(tags.get(DT_VERDEFNUM, [0])[0] if off is not None else 0):
        _, _, _, cnt, _, aux, nxt = elf.u("HHHHIII", off)
        a = off + aux
        for _ in range(cnt):
            name, anext = elf.u("II", a)
            refs.append((name, "version definition", False))
            a += anext
        if not nxt:
            break
        off += nxt
    # Version needs: Verneed {vn_version, vn_cnt (H), vn_file, vn_aux,
    # vn_next (I)}, Vernaux {vna_hash, vna_flags, vna_other (H, H),
    # vna_name, vna_next}.  vn_file names a library that is also a NEEDED
    # entry, and ld shares the string, so it lies in the name block.
    off = at(DT_VERNEED)
    for _ in range(tags.get(DT_VERNEEDNUM, [0])[0] if off is not None else 0):
        _, cnt, vfile, aux, nxt = elf.u("HHIII", off)
        refs.append((vfile, "version need file", False))
        a = off + aux
        for _ in range(cnt):
            _, _, _, name, anext = elf.u("IHHII", a)
            refs.append((name, "version need", False))
            a += anext
        if not nxt:
            break
        off += nxt
    return refs, tags


def string_at(elf, strtab, strsz, off):
    if off >= strsz:
        return None
    start = strtab + off
    end = elf.d.find(b"\0", start, strtab + strsz)
    if end < 0:
        return None
    return elf.d[start:end].decode("latin-1")


def check(path, verbose):
    """Returns (ok, lines)."""
    elf = Elf(path)
    dyn = elf.dynamic()
    if dyn is None:
        return True, ["not dynamic"]
    refs, tags = references(elf, dyn)
    if DT_STRTAB not in tags or DT_STRSZ not in tags:
        raise NotElf("dynamic section without DT_STRTAB or DT_STRSZ")
    strtab = elf.file_offset(tags[DT_STRTAB][0])
    strsz = tags[DT_STRSZ][0]
    if strtab is None:
        raise NotElf("DT_STRTAB outside every PT_LOAD segment")

    bad = []
    names = []                                   # (start, end, label, string)
    for off, what, is_name in refs:
        s = string_at(elf, strtab, strsz, off)
        if s is None:
            bad.append("%s at %d: outside .dynstr (DT_STRSZ %d)" % (what, off, strsz))
        elif is_name:
            names.append((off, off + len(s) + 1, what, s))
    if not names:
        return not bad, bad or ["no library names"]

    def in_names(off):
        return any(start <= off < end for start, end, _, _ in names)

    # The earliest string that is not part of a library name.
    others = [(off, what) for off, what, is_name in refs
              if not is_name and not in_names(off) and off < strsz]
    first_other = min(others) if others else None
    for start, _, what, s in sorted(names):
        if first_other is not None and start > first_other[0]:
            bad.append("%s [%s] at %d, after %s [%s] at %d" % (
                what, s, start, first_other[1],
                string_at(elf, strtab, strsz, first_other[0]), first_other[0]))
    lines = bad
    if verbose or not bad:
        block_end = max(end for _, end, _, _ in names)
        info = ["%s [%s] at %d" % (what, s, start) for start, _, what, s in sorted(names)]
        info.append("names end at %d of %d; first other string at %s" % (
            block_end, strsz, first_other[0] if first_other else "none"))
        lines = bad + (info if verbose else [info[-1]])
    return not bad, lines


def main(argv):
    verbose = False
    files = []
    for a in argv[1:]:
        if a == "-v":
            verbose = True
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            files.append(a)
    if not files:
        print("usage: check-dynstr-order.py [-v] FILE...", file=sys.stderr)
        return 2
    status = 0
    for path in files:
        try:
            ok, lines = check(path, verbose)
        except (OSError, NotElf, struct.error) as e:
            print("%s: ERROR: %s" % (path, e))
            status = max(status, 2)
            continue
        print("%s: %s" % (path, "ok" if ok else "FAIL: library names not first in .dynstr"))
        for line in lines:
            print("    " + line)
        if not ok:
            status = max(status, 1)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
