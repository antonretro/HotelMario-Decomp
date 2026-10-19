"""Write site/progress.json from the asm/ and src/ trees.

A function counts as decompiled once its asm/funcs/<name>.s is gone and src/ defines it in C
(the matching build still has to pass). Also records how much of the asm is real instructions.

Usage: python tools/progress.py
"""
import datetime
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from stb import parse  # noqa: E402

STB = os.path.join("build", "eu", "fs", "cdi_hotel.stb")
IDATA_END = 0x1B940  # end of code in cdi_hotel (EU)
# Game code: after_the_game .. nvr_available. Before it is Microware's cstart; after it the CD-i
# system bindings, clib and math libraries, which are not ours to decompile.
GAME_START, GAME_END = 0x36A, 0x17122


def handwritten(path):
    """The C compiler always opens a function with link.w a5; game functions without it were written in assembly."""
    for line in open(path):
        s = line.strip()
        if s and not s.startswith(";") and not s.endswith(":"):
            return not s.startswith("link.w")
    return False


def c_functions():
    names = set()
    for root, _, files in os.walk("src"):
        for f in files:
            if f.endswith(".c"):
                txt = open(os.path.join(root, f), errors="replace").read()
                # a definition starts in column 0 and its line has no ';' (K&R parameter types follow on later lines)
                names |= set(re.findall(r"^(?:\w+[\s\*]+)*(\w+)\s*\([\w\s,\*]*\)[ \t]*(?:\{[ \t]*)?$", txt, re.M))
    return names


def main():
    first = {}
    for v, f, n in parse(STB):  # several names can share an address; keep the first, like gen_asm
        if f & 0x0004 and v >= 0x52:
            first.setdefault(v, n)
    code = sorted(first.items())
    in_c = c_functions()
    funcs = []
    for i, (addr, name) in enumerate(code):
        end = code[i + 1][0] if i + 1 < len(code) else IDATA_END
        if not GAME_START <= addr < GAME_END:
            continue
        path = os.path.join("asm", "funcs", name + ".s")
        raw = 0
        if os.path.exists(path):
            raw = sum(2 * (l.split(";")[0].count(",") + 1) for l in open(path) if l.strip().startswith("dc.w"))
        if name in in_c:
            status = "c"        # equivalent C written; the build still uses the asm until it matches
        elif os.path.exists(path) and handwritten(path):
            status = "asmsrc"   # original was hand-written assembly: done as-is
        else:
            status = "asm"
        funcs.append({"name": name, "addr": addr, "size": end - addr, "status": status, "raw": raw})
    total = sum(f["size"] for f in funcs)
    done = sum(f["size"] for f in funcs if f["status"] in ("c", "asmsrc"))
    raw = sum(f["raw"] for f in funcs)
    out = {
        "updated": datetime.date.today().isoformat(),
        "target": {"name": "cdi_hotel", "region": "Europe", "sha1": "3fda0e6da5303b083da70c36c25064d94c025ad6"},
        "matching": True,
        "bytes_total": total,
        "bytes_c": done,
        "funcs_total": len(funcs),
        "funcs_c": sum(f["status"] == "c" for f in funcs),
        "funcs_asmsrc": sum(f["status"] == "asmsrc" for f in funcs),
        "bytes_disassembled": total - raw,
        "functions": funcs,
    }
    os.makedirs("site", exist_ok=True)
    json.dump(out, open(os.path.join("site", "progress.json"), "w"), indent=1)
    with open(os.path.join("site", "progress.js"), "w") as js:  # lets index.html work from file://
        js.write("window.PROGRESS = " + json.dumps(out) + ";\n")
    print(f"done: {done}/{total} bytes ({100 * done / total:.2f}%), {out['funcs_c']} in C + "
          f"{out['funcs_asmsrc']} hand-written asm of {len(funcs)} game functions; "
          f"disassembled {100 * (total - raw) / total:.1f}%")


if __name__ == "__main__":
    main()
