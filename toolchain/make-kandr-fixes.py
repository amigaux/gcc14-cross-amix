#!/usr/bin/env python3
"""Rewrite AMIX's K&R function definitions into ANSI ones, for C++.

SVR4.0 implements some libc entry points as `static` functions defined inside
the header -- stat(), lstat() and fstat() are wrappers around the versioned
_xstat() family -- and it writes them in K&R style:

    static int
    stat(path, buf)
    const char *path;
    struct stat *buf;
    { ... }

C has accepted that since 1978. C++ never has, so **every C++ translation unit
that includes <sys/stat.h> fails to compile**, with errors that point at the
system header and read as if the program were at fault:

    error: 'int stat' redeclared as different kind of entity
    error: 'path' was not declared in this scope

Two of them, sys/mkdev.h and ftw.h, also declare the function extern before
defining it static, which GCC 14 rejects in C as well; those declarations are
made static here too (make_static below).

Five headers in a stock AMIX installation do this: sys/stat.h, sys/utsname.h,
sys/mkdev.h, ftw.h and regexp.h. The first two are on the path of anything that
touches files or asks the machine its name, so this is not avoidable by
discipline.

The output goes into GCC's include-fixed, which is searched before the sysroot
-- the mechanism that exists for exactly this, a system header that is wrong or
older than the language. The vendor tree is not modified.

WHAT THIS DOES NOT DO: it is not a general K&R parser. It rewrites definitions
whose parameter declarations are one per line in the ordinary form, verifies
that every named parameter was found, and leaves anything it does not fully
understand untouched -- an unconverted definition fails loudly at compile time,
which is the right way round. It reports what it converted so the count can be
checked against expectation.

Generated output contains AMIX's own code and must never be committed.

Usage: make-kandr-fixes.py <sysroot> <include-fixed dir>
"""
import os
import re
import sys

sysroot, fixed_dir = sys.argv[1], sys.argv[2]
incdir = os.path.join(sysroot, "usr/include")

# A signature line: name(a, b, c) with bare identifiers and nothing after it.
# The storage class and return type are usually on the line above; ftw.h puts
# them on the same line ("static int ftw(path, fn, depth)"), so allow that.
SIG = re.compile(r"^(\s*(?:static\s+[A-Za-z_][\w\s]*?[\s*])?)([A-Za-z_]\w*)\s*\(\s*([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*\)\s*$")
# A parameter declaration line, possibly declaring several names.
DECL = re.compile(r"^\s*((?:const\s+|volatile\s+|unsigned\s+|signed\s+|struct\s+|union\s+|enum\s+|register\s+)*[A-Za-z_]\w*)\s+([^;]+);\s*$")
# A one-function declaration with a parameter list: [extern|static] type name(...);
PROTO = re.compile(r"^(\s*)(extern\s+|static\s+)?([A-Za-z_][\w\s*]*?[\s*])([A-Za-z_]\w*)\s*\((.*)\)\s*;\s*$")


def balanced(text):
    """True if text never closes a parenthesis it did not open, and ends level."""
    depth = 0
    for c in text:
        depth += (c == "(") - (c == ")")
        if depth < 0:
            return False
    return depth == 0


def split_args(text):
    """Split a parameter list at its top-level commas."""
    out, depth, cur = [], 0, ""
    for c in text:
        depth += (c == "(") - (c == ")")
        if c == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += c
    return out + [cur.strip()]


def prototypes(lines):
    """name -> parameter types, from the ANSI prototypes in the header.

    Used for a definition whose old-style parameters the declaration parser
    cannot read -- ftw.h's `int (*fn) ();` -- and the prototype is the better
    source anyway: in C++ an empty () means no arguments, so copying the K&R
    type would declare a second ftw() that the program's calls never reach."""
    out = {}
    for line in lines:
        m = PROTO.match(line)
        if m and m.group(5).strip() and balanced(m.group(5)):
            out.setdefault(m.group(4), split_args(m.group(5)))
    return out


def with_name(ptype, name):
    """Put a parameter name into an abstract type: 'int (*)(int)' -> 'int (*fn)(int)'."""
    if "(*)" in ptype:
        return ptype.replace("(*)", "(*%s)" % name, 1)
    return ptype + name if ptype.endswith("*") else "%s %s" % (ptype, name)


def split_declarators(text):
    """'*path, buf[]' -> [('*', 'path'), ('', 'buf[]')] keeping pointer depth."""
    out = []
    for piece in text.split(","):
        piece = piece.strip()
        stars = ""
        while piece.startswith("*"):
            stars += "*"
            piece = piece[1:].strip()
        out.append((stars, piece))
    return out


def convert(text):
    lines = text.split("\n")
    protos = prototypes(lines)
    out, i, converted, static = [], 0, [], []
    while i < len(lines):
        m = SIG.match(lines[i])
        if not m:
            out.append(lines[i]); i += 1; continue

        indent, name, params = m.group(1), m.group(2), m.group(3)
        names = [p.strip() for p in params.split(",")]

        # Collect the declaration lines that follow, up to the opening brace.
        j, types = i + 1, {}
        while j < len(lines):
            line = lines[j]
            if line.strip().startswith("{"):
                break
            d = DECL.match(line)
            if not d:
                types = None; break
            base = d.group(1).strip()
            for stars, ident in split_declarators(d.group(2)):
                array = ""
                if ident.endswith("]"):
                    k = ident.index("[")
                    array, ident = ident[k:], ident[:k]
                types[ident] = "%s %s%s%s" % (base, stars, ident, array)
            j += 1

        if types is None or j >= len(lines) or any(n not in types for n in names):
            # Fall back on the header's own prototype, if the lines up to the
            # brace are all declarations and it names as many parameters.
            j = i + 1
            while j < len(lines) and lines[j].strip().endswith(";"):
                j += 1
            ptypes = protos.get(name)
            if (j < len(lines) and lines[j].strip().startswith("{")
                    and ptypes and len(ptypes) == len(names)):
                types = {n: with_name(t, n) for n, t in zip(names, ptypes)}
            else:
                # Only rewrite when every parameter was accounted for.
                out.append(lines[i]); i += 1; continue

        prev = next((l for l in reversed(out) if l.strip()), "")
        if "static" in indent.split() or prev.split()[:1] == ["static"]:
            static.append(name)
        out.append("%s%s(%s)" % (indent, name, ", ".join(types[n] for n in names)))
        converted.append(name)
        i = j
    return "\n".join(out), converted, static


def make_static(text, names):
    """Give `static` to the declarations of functions the header defines static.

    sys/mkdev.h and ftw.h declare makedev(), major(), minor() and ftw() with
    external linkage, then define them static further down.  C has called that
    undefined since C89; gcc 2.7.2.3 warned, GCC 14 stops with "static
    declaration of 'makedev' follows non-static declaration", and g++ with
    "was declared 'extern' and later 'static'".  Only a declaration of that one
    function is changed, never a line that declares several."""
    out = []
    for line in text.split("\n"):
        m = PROTO.match(line)
        if (m and m.group(4) in names and (m.group(2) or "").strip() != "static"
                and balanced(m.group(5)) and "," not in m.group(3)):
            line = "%sstatic %s%s(%s);" % (m.group(1), m.group(3), m.group(4), m.group(5))
        out.append(line)
    return "\n".join(out)


total = 0
for rel in ("sys/stat.h", "sys/utsname.h", "sys/mkdev.h", "ftw.h", "regexp.h"):
    src = os.path.join(incdir, rel)
    if not os.path.exists(src):
        continue
    text = open(src, errors="replace").read()
    new, names, static = convert(text)
    if not names:
        continue
    new = make_static(new, static)
    dst = os.path.join(fixed_dir, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    # These sit in include-fixed beside the generated linkage wrappers and are
    # seen instead of them, so they have to carry the same extern "C" guard.
    header = ('/* Generated by toolchain/make-kandr-fixes.py from the AMIX sysroot.\n'
              ' * K&R function definitions rewritten to ANSI so C++ can read this\n'
              " * header, and wrapped in the linkage guard the original lacks.\n"
              ' * Contains AMIX code: do not commit, do not publish. */\n'
              '#ifdef __cplusplus\nextern "C" {\n#endif\n')
    footer = '\n#ifdef __cplusplus\n}\n#endif\n'
    open(dst, "w").write(header + new + footer)
    print("  %-18s %s" % (rel, ", ".join(names)))
    total += len(names)

if total == 0:
    sys.exit("make-kandr-fixes.py: converted nothing -- check the sysroot path")
print("make-kandr-fixes.py: %d definitions rewritten" % total)
