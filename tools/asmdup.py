#!/usr/bin/env python3
import glob
import hashlib
import os
import re
import subprocess
import sys

OBJDIR = "build/obj"
MIN_INSTRS = 12

HEADER_RE = re.compile(r"^([0-9a-f]+)\s+<([^>]+)>:\s*$")
INSTR_RE = re.compile(r"^\s*([0-9a-f]+):\s+(.*)$")
RELOC_RE = re.compile(r"^\s*[0-9a-f]+:\s+(\S+)\s+(\S+)")
BRANCH_TARGET_RE = re.compile(r"\b0x[0-9a-f]+\s+<[^>]*>")


SRC_EXTS = (".c", ".m", ".mm", ".cpp", ".cc")


def build_source_map():
    """Map nob-encoded object basename -> real source path.

    nob encodes a source path by replacing '/' with '.', so directories with
    literal dots (music.theory) are irreversible from the name alone. We instead
    encode every existing source forward and match."""
    smap: dict[str, str] = {}
    for root in ("modules", "apps", "lib"):
        for dirpath, _, files in os.walk(root):
            for f in files:
                if f.endswith(SRC_EXTS):
                    rel = os.path.join(dirpath, f)
                    smap[rel.replace("/", ".") + ".o"] = rel
    return smap


def normalize_instr(text: str, addr, func_start, reloc_sym) -> str:
    text = text.split(";")[0]  # drop objdump comment annotations (e.g. "; =48")

    def repl(m):
        tok = m.group(0)
        if reloc_sym:
            return "<reloc>"
        hexval = int(tok.split()[0], 16)
        return "<rel%+d>" % (hexval - func_start)

    text = BRANCH_TARGET_RE.sub(repl, text)
    text = re.sub(r"\s+", " ", text).strip()
    if reloc_sym:
        text += " |call=" + reloc_sym
    return text


def analyze_object(path: str) -> list[str]:
    """Return list of (func_name, instr_count, sha1) for one object file."""
    try:
        out = subprocess.run(
            ["objdump", "-d", "-r", "--no-show-raw-insn", path],
            capture_output=True, text=True, check=True,
        ).stdout
    except subprocess.CalledProcessError:
        return []

    funcs = []
    cur_name = None
    cur_start = 0
    body : list[str] = []
    pending = None  # (addr, text) of last instruction awaiting possible reloc

    def flush_pending(reloc_sym : re.Match[str] | None = None):
        nonlocal pending
        if pending is not None:
            addr, text = pending
            body.append(normalize_instr(text, addr, cur_start, reloc_sym))
            pending = None

    def flush_func():
        nonlocal body
        flush_pending()
        if cur_name and not cur_name.startswith("ltmp") and len(body) >= MIN_INSTRS:
            h = hashlib.sha1("\n".join(body).encode()).hexdigest()
            funcs.append((cur_name, len(body), h))
        body = []

    for line in out.splitlines():
        hm = HEADER_RE.match(line)
        if hm:
            flush_func()
            cur_start = int(hm.group(1), 16)
            cur_name = hm.group(2)
            continue
        im = INSTR_RE.match(line)
        if im:
            flush_pending()
            pending = (int(im.group(1), 16), im.group(2))
            continue
        rm = RELOC_RE.match(line)
        if rm and pending is not None:
            flush_pending(reloc_sym=rm.group(2))
            continue
    flush_func()
    return funcs


def main():
    smap = build_source_map()
    all_objs = glob.glob(os.path.join(OBJDIR, "*.o"))
    objs = sorted(p for p in all_objs if os.path.basename(p) in smap)
    stale = sorted(os.path.basename(p) for p in all_objs
                   if os.path.basename(p) not in smap)
    groups = {}  # hash -> list of (func, count, srcpath)
    total = 0
    for o in objs:
        src = smap[os.path.basename(o)]
        for name, count, h in analyze_object(o):
            total += 1
            groups.setdefault(h, []).append((name, count, src))

    dups = {h: g for h, g in groups.items() if len(g) > 1}
    cross = []  # distinct function names -> real copy-paste smell
    same = []   # one name across TUs -> header static/inline duplication
    for h, g in dups.items():
        names = {name for name, _, _ in g}
        (cross if len(names) > 1 else same).append((h, g))

    print("scanned %d live object files (%d stale .o skipped), " +
          "%d functions (>=%d instrs)"
          % (len(objs), len(stale), total, MIN_INSTRS))
    print("%d cross-name groups (copy-paste smell), %d same-name groups " +
          "(header static/inline)\n" % (len(cross), len(same)))

    def dump(title, items):
        print("########## %s ##########\n" % title)
        for h, g in sorted(items, key=lambda kv: (-kv[1][0][1], -len(kv[1]))):
            count = g[0][1]
            print("=== %d funcs, %d instrs each [%s]" % (len(g), count, h[:10]))
            for name, _, src in sorted(g):
                print("    %-44s  %s" % (name, src))
            print()

    dump("CROSS-NAME (copy-paste smell)", cross)
    dump("SAME-NAME (header static/inline emitted per TU)", same)


if __name__ == "__main__":
    main()
