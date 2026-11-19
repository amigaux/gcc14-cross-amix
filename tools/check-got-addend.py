#!/usr/bin/env python3
"""check-got-addend.py -- find m68k GOT-offset relocations with an addend.

WHY
---
In -fpic/-fPIC code GCC reaches a global through its GOT slot:
`move.l sym@GOT(%a5),%a0` loads the slot, which holds the address of sym,
and the code adds any offset into sym itself afterwards.  The assembler
records the load as R_68K_GOT32O (GOT16O with -fpic, GOT8O for the byte
form): "the offset of sym's slot from the GOT pointer".  An addend on one of
those, `sym+2@GOT`, has no meaning there, and m68k ld drops it without a
word ("This relocation does not use the addend", bfd/elf32-m68k.c): the
code gets the address of sym, not of sym+2, and reads or writes two bytes
early.  Nothing warns -- not gas, not ld, not the runtime linker.  GCC 14
has been seen to emit that form for m68k, so every PIC object is scanned
for it.

WHAT IS CHECKED
---------------
Every SHT_RELA section that applies to a section, in relocatable objects
and archives of them.  A hit is R_68K_GOT32O, R_68K_GOT16O or R_68K_GOT8O
with a non-zero addend.  R_68K_GOT32/16/8 are left alone: they are
PC-relative (`lea (%pc,_GLOBAL_OFFSET_TABLE_@GOTPC),%a5` is a GOT32 with
addend 2), so their addend is the PC bias, not an offset into the GOT.

Only objects can be checked.  ld zeroes the addend as it links, so a shared
object or program shows nothing even with -Wl,--emit-relocs (measured:
ext_rec@GOT and ext_rec+2@GOT linked to the same slot, both emitted with
addend 0).  A linked file is reported as "not checkable" and fails (exit 2)
rather than passing quietly; --skip-unrelocated makes it a skip, for a gate
that is handed a package's whole tree and checks its objects as well.

Usage:
    check-got-addend.py [-v] [--skip-unrelocated] FILE...
        FILE: .o or .a

Exit status: 0 no hits; 1 hits, each listed; 2 a file could not be read or
checked.
"""
import struct
import sys

EM_68K = 4
SHT_SYMTAB, SHT_RELA, SHT_REL, SHT_DYNSYM = 2, 4, 9, 11
ET_REL = 1
R_GOT_PCREL = {7: "R_68K_GOT32", 8: "R_68K_GOT16", 9: "R_68K_GOT8"}
R_GOT_OFFSET = {10: "R_68K_GOT32O", 11: "R_68K_GOT16O", 12: "R_68K_GOT8O"}
R_PLT = range(13, 19)              # R_68K_PLT32 .. R_68K_PLT8O: calls


class Unreadable(Exception):
    pass


def cstr(data, off):
    end = data.find(b"\0", off)
    return data[off:end if end >= 0 else len(data)].decode("latin-1")


def scan_elf(data, name):
    """Returns (offset_relocs, got_relocs, pic, hits, checkable)."""
    if data[:4] != b"\x7fELF":
        raise Unreadable("not an ELF file")
    if data[4] != 1 or data[5] != 2:
        raise Unreadable("not 32-bit big-endian ELF")

    def u(fmt, off):
        if off < 0 or off + struct.calcsize(fmt) > len(data):
            raise Unreadable("truncated at offset %#x" % off)
        return struct.unpack_from(">" + fmt, data, off)

    e_type, e_machine = u("HH", 16)
    if e_machine != EM_68K:
        raise Unreadable("not an m68k object (e_machine %d)" % e_machine)
    e_shoff, = u("I", 0x20)
    e_shentsize, e_shnum, e_shstrndx = u("HHH", 0x2E)
    if not e_shoff or not e_shnum:
        return 0, 0, False, [], False
    sh = [u("IIIIIIIIII", e_shoff + i * e_shentsize) for i in range(e_shnum)]
    shstr = sh[e_shstrndx][4] if e_shstrndx < e_shnum else None

    def secname(i):
        if shstr is None or i >= len(sh):
            return "section %d" % i
        return cstr(data, shstr + sh[i][0]) or "section %d" % i

    symtabs = [i for i, s in enumerate(sh) if s[1] == SHT_SYMTAB]
    got = 0
    offset_relocs = 0
    pic = False
    hits = []
    if e_type != ET_REL:
        return 0, 0, False, [], False
    for i, s in enumerate(sh):
        sh_type, sh_offset, sh_size, sh_link, sh_info, sh_entsize = (
            s[1], s[4], s[5], s[6], s[7], s[9])
        if sh_type == SHT_REL:
            raise Unreadable("%s is SHT_REL; m68k uses SHT_RELA" % secname(i))
        if sh_type != SHT_RELA:
            continue
        # A static relocation section applies to a section (sh_info) and
        # names its symbols in .symtab.  The dynamic ones (.rela.dyn,
        # .rela.plt) name .dynsym and are not what the compiler wrote.
        link = sh_link
        if link < len(sh) and sh[link][1] == SHT_DYNSYM:
            continue
        if sh_info == 0 or sh_info >= len(sh):
            continue
        if link >= len(sh) or sh[link][1] != SHT_SYMTAB:
            # AMIX's 1991 X archives point sh_link at the section itself
            # (tools/fix-elf-relalink.py); with one .symtab there is no
            # doubt which it means.
            if len(symtabs) != 1:
                raise Unreadable("%s: sh_link names no symbol table" % secname(i))
            link = symtabs[0]
        sym_off, sym_size, str_idx = sh[link][4], sh[link][5], sh[link][6]
        str_off = sh[str_idx][4] if str_idx < len(sh) else None
        ent = sh_entsize or 12
        for o in range(sh_offset, sh_offset + sh_size - ent + 1, ent):
            r_offset, r_info, r_addend = u("IIi", o)
            r_type, r_sym = r_info & 0xFF, r_info >> 8
            if r_type in R_PLT:
                pic = True
                continue
            if r_type in R_GOT_PCREL:
                got += 1
                continue
            if r_type not in R_GOT_OFFSET:
                continue
            got += 1
            offset_relocs += 1
            if r_addend == 0:
                continue
            sym = "symbol %d" % r_sym
            if r_sym * 16 + 16 <= sym_size and str_off is not None:
                st_name, = u("I", sym_off + r_sym * 16)
                sym = cstr(data, str_off + st_name) or sym
            hits.append("%s: %s+%#x: %s %s%+d" % (
                name, secname(sh_info), r_offset, R_GOT_OFFSET[r_type], sym, r_addend))
    return offset_relocs, got, pic or got > 0, hits, True


def members(data, path):
    """The members of an ar archive, as (name, bytes)."""
    pos, longnames = 8, b""
    while pos + 60 <= len(data):
        hdr = data[pos:pos + 60]
        if hdr[58:60] != b"`\n":
            raise Unreadable("bad archive member header at %#x" % pos)
        raw = hdr[:16].decode("latin-1").rstrip()
        size = int(hdr[48:58].decode("latin-1").strip() or "0")
        body = data[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
        if raw == "//":
            longnames = body
            continue
        if raw in ("/", "/SYM64/", "__.SYMDEF", "__.SYMDEF SORTED"):
            continue
        if raw.startswith("#1/"):                      # BSD long name
            n = int(raw[3:])
            name, body = body[:n].rstrip(b"\0").decode("latin-1"), body[n:]
        elif raw.startswith("/") and raw[1:].isdigit():  # SysV long name
            o = int(raw[1:])
            end = longnames.find(b"/\n", o)
            name = longnames[o:end if end >= 0 else None].decode("latin-1")
        else:
            name = raw[:-1] if raw.endswith("/") else raw
        yield "%s(%s)" % (path, name), body


def main(argv):
    verbose = skip_unrelocated = False
    files = []
    for a in argv[1:]:
        if a == "-v":
            verbose = True
        elif a == "--skip-unrelocated":
            skip_unrelocated = True
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            files.append(a)
    if not files:
        print("usage: check-got-addend.py [-v] [--skip-unrelocated] FILE...", file=sys.stderr)
        return 2

    status = 0
    n_obj = n_pic = n_got = n_offset = 0
    all_hits = []
    for path in files:
        try:
            with open(path, "rb") as f:
                data = f.read()
            if data[:8] == b"!<arch>\n":
                items = members(data, path)
            elif data[:4] == b"\x7fELF":
                items = [(path, data)]
            else:
                raise Unreadable("neither ELF nor an archive")
            for name, body in items:
                if body[:4] != b"\x7fELF":
                    if verbose:
                        print("%s: skipped, not ELF" % name)
                    continue
                offset_relocs, got, pic, hits, checkable = scan_elf(body, name)
                if not checkable:
                    if skip_unrelocated:
                        print("%s: skipped, not a relocatable object" % name)
                        continue
                    print("%s: not checkable: ld drops the addend when it links; "
                          "check the objects" % name)
                    status = max(status, 2)
                    continue
                n_obj += 1
                n_got += got
                n_offset += offset_relocs
                if pic:
                    n_pic += 1
                all_hits += hits
                if verbose:
                    print("%s: %d GOT relocations, %d GOT-offset, %d with an addend"
                          % (name, got, offset_relocs, len(hits)))
        except (OSError, Unreadable, ValueError) as e:
            print("%s: ERROR: %s" % (path, e))
            status = max(status, 2)
    for h in all_hits:
        print("HIT  " + h)
    print("check-got-addend: %d objects, %d PIC (GOT or PLT relocations), %d GOT relocations "
          "(%d GOT-offset), %d with an addend" % (n_obj, n_pic, n_got, n_offset, len(all_hits)))
    if all_hits:
        status = max(status, 1)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
