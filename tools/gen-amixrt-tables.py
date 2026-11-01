#!/usr/bin/env python3
"""gen-amixrt-tables.py -- write libamixrt's message tables from libc.

    gen-amixrt-tables.py SYSROOT/usr/ccs/lib/libc.so > amixrt_tables.c

sys_errlist, sys_nerr, _sys_siglist and _sys_nsig exist in AMIX only as
static members of usr/ccs/lib/libc.so (errlst.o and siglist.o), which cannot
go into a shared library.  libamixrt.so carries its own copies, and they have
to read exactly as libc.so.1's strerror and psignal do.  So they are taken from
those two members of the local sysroot at build time, like the C++ linkage
wrappers, and nothing from the AMIX installation enters the repository.

The archive is the SVR4 ar format GNU ar also writes.  Each member is a
32-bit big-endian m68k ELF relocatable object; a table is an array of
pointers, each one an R_68K_32 relocation against a local symbol in .data,
and the strings are read from there.  Standard library only, so the build
needs nothing beyond python3.
"""
import struct
import sys


def ar_members(data):
    if data[:8] != b"!<arch>\n":
        sys.exit("gen-amixrt-tables.py: not an ar archive")
    pos, names = 8, b""
    while pos + 60 <= len(data):
        hdr = data[pos:pos + 60]
        name = hdr[:16].rstrip()
        size = int(hdr[48:58].strip())
        body = data[pos + 60:pos + 60 + size]
        if name == b"//":
            names = body
        elif name.startswith(b"/") and name[1:].isdigit():
            off = int(name[1:])
            name = names[off:names.index(b"/\n", off)]
            yield name.decode(), body
        elif name != b"/":
            yield name.rstrip(b"/").decode(), body
        pos += 60 + size + (size & 1)


class Elf:
    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
            sys.exit("gen-amixrt-tables.py: not a 32-bit big-endian ELF object")
        self.d = data
        shoff, = struct.unpack_from(">I", data, 32)
        shentsize, shnum, shstrndx = struct.unpack_from(">HHH", data, 46)
        self.sh = []
        for i in range(shnum):
            f = struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize)
            self.sh.append(dict(name=f[0], type=f[1], offset=f[4], size=f[5],
                                link=f[6], info=f[7], entsize=f[9]))
        self.syms = []
        for s in self.sh:
            if s["type"] == 2:  # SHT_SYMTAB
                strtab = self.sh[s["link"]]
                for j in range(s["size"] // 16):
                    name, value, size, info, other, shndx = struct.unpack_from(
                        ">IIIBBH", data, s["offset"] + j * 16)
                    self.syms.append(dict(name=self.cstr(strtab, name), value=value,
                                          size=size, shndx=shndx))
        self.rela = {}  # section index -> {offset: (symbol, addend)}
        for s in self.sh:
            if s["type"] == 4:  # SHT_RELA
                tab = self.rela.setdefault(s["info"], {})
                for j in range(s["size"] // 12):
                    off, info, addend = struct.unpack_from(">IIi", data, s["offset"] + j * 12)
                    if info & 0xff != 1:  # R_68K_32
                        sys.exit("gen-amixrt-tables.py: unexpected relocation type %d" % (info & 0xff))
                    tab[off] = (self.syms[info >> 8], addend)

    def cstr(self, sec, off):
        start = sec["offset"] + off
        return self.d[start:self.d.index(b"\0", start)].decode("latin-1")

    def sym(self, name):
        for s in self.syms:
            if s["name"] == name:
                return s
        sys.exit("gen-amixrt-tables.py: no symbol %s" % name)

    def word(self, name):
        s = self.sym(name)
        return struct.unpack_from(">i", self.d, self.sh[s["shndx"]]["offset"] + s["value"])[0]

    def strings(self, name, count):
        s = self.sym(name)
        relocs = self.rela.get(s["shndx"], {})
        out = []
        for k in range(count):
            r = relocs.get(s["value"] + 4 * k)
            if r is None:
                out.append(None)
                continue
            target, addend = r
            out.append(self.cstr(self.sh[target["shndx"]], target["value"] + addend))
        return out


def c_string(s):
    out = []
    for ch in s.encode("latin-1"):
        c = chr(ch)
        if c in '"\\':
            out.append("\\" + c)
        elif 32 <= ch < 127:
            out.append(c)
        else:
            out.append("\\%03o" % ch)
    return '"' + "".join(out) + '"'


def table(name, strings):
    lines = ["char *%s[] = {" % name]
    for s in strings:
        lines.append("\t%s," % ("0" if s is None else c_string(s)))
    lines.append("};")
    return "\n".join(lines)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: gen-amixrt-tables.py SYSROOT/usr/ccs/lib/libc.so")
    members = dict(ar_members(open(sys.argv[1], "rb").read()))
    for m in ("errlst.o", "siglist.o"):
        if m not in members:
            sys.exit("gen-amixrt-tables.py: %s has no %s" % (sys.argv[1], m))
    err = Elf(members["errlst.o"])
    sig = Elf(members["siglist.o"])
    nerr = err.word("_sys_nerr")
    nsig = sig.word("_sys_nsig")
    errlist = err.strings("_sys_errlist", err.sym("_sys_errlist")["size"] // 4)
    siglist = sig.strings("_sys_siglist", sig.sym("_sys_siglist")["size"] // 4)
    print("/* amixrt_tables.c -- generated by tools/gen-amixrt-tables.py from the")
    print(" * sysroot's usr/ccs/lib/libc.so (errlst.o, siglist.o).  Do not edit. */")
    print()
    print(table("_sys_errlist", errlist))
    print("extern char *sys_errlist[%d] __attribute__((alias(\"_sys_errlist\")));" % len(errlist))
    print("int _sys_nerr = %d;" % nerr)
    print("extern int sys_nerr __attribute__((alias(\"_sys_nerr\")));")
    print()
    print(table("_sys_siglist", siglist))
    print("int _sys_nsig = %d;" % nsig)


main()
