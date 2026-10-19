"""Generate vasm (Motorola syntax) source for cdi_hotel, one file per function, verified per instruction.

Usage: python tools/gen_asm.py <baserom module> <stb> <asm_out_dir>

Every instruction from capstone is re-assembled with vasm at its original address; any instruction whose
bytes differ (or which vasm rejects) is emitted as dc.w with the disassembly in a comment.
"""
import os
import re
import struct
import subprocess
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from stb import parse  # noqa: E402

VASM = os.path.join(HERE, "ext", "vasm", "vasmm68k_mot")
VASM_FLAGS = ["-quiet", "-no-opt", "-m68000", "-Fbin"]
CODE_FLAG = 0x0004
BRANCH = re.compile(r"^(b\w+|db\w+|jsr|jmp|bsr)(\.\w)?$")


def to_vasm(mn, ops):
    if "invalid" in ops:
        return None
    ops = re.sub(r",\s+", ",", ops)
    if mn == "moveq":  # 8-bit signed immediate
        ops = re.sub(r"^#\$([89a-f][0-9a-f])\b", lambda m: f"#-${0x100 - int(m.group(1), 16):x}", ops)
    # capstone prints bit ops on a data register as .b; on Dn they are long-sized
    if re.fullmatch(r"b(set|clr|chg|tst)\.b", mn) and re.search(r"(^|,)d[0-7]$", ops):
        mn = mn[:-1] + "l"
    # absolute short addresses are sign-extended: $ffff.w means -1.w to vasm
    ops = re.sub(r"\$([89a-f][0-9a-f]{3})\.w", lambda m: f"-${0x10000 - int(m.group(1), 16):x}.w", ops)
    return f"{mn} {ops}".strip()


def decode(d, start, end, code_syms):
    md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
    funcs = sorted(code_syms)
    out = []  # (pc, size, text or None)
    pc = start
    while pc < end:
        nxt = min([f for f in funcs if f > pc] + [end])
        ins = list(md.disasm(d[pc:nxt], pc, count=1))
        if not ins:
            out.append((pc, 2, None))
            pc += 2
            continue
        i = ins[0]
        out.append((pc, i.size, to_vasm(i.mnemonic, i.op_str) if "invalid" not in i.mnemonic else None))
        pc += i.size
    return out


def run_vasm(src, out, lst=None):
    args = [VASM] + VASM_FLAGS + (["-L", lst] if lst else []) + ["-o", out, src]
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def verify(d, insns, tmp):
    """Return set of pcs whose text does not reproduce the original bytes."""
    bad = {pc for pc, _, t in insns if t is None}
    for _ in range(2000):  # vasm stops at the first fatal error, so this can take many rounds
        src = os.path.join(tmp, "verify.s")
        lines = []
        lineno = {}
        for pc, size, t in insns:
            if pc in bad:
                continue
            lines.append(f" org ${pc:x}")
            lines.append(f" {t}")
            lineno[len(lines)] = pc
        open(src, "w").write("\n".join(lines) + "\n")
        lst = os.path.join(tmp, "verify.lst")
        rc, msg = run_vasm(src, os.path.join(tmp, "verify.bin"), lst)
        errs = {int(m) for m in re.findall(r"error \d+ in line (\d+)", msg)}
        if errs:
            bad |= {lineno[l] for l in errs if l in lineno}
            continue
        # an instruction that assembles too long overlaps the next one's section
        ov = re.search(r"sections <[^>]*>:([0-9a-f]+)-[0-9a-f]+ and", msg, re.I)
        if ov:
            bad.add(int(ov.group(1), 16))
            continue
        if rc:
            raise SystemExit("vasm failed during verify:\n" + msg[-2000:])
        # parse listing: "SS:AAAAAAAA HEX ... \t LINE: text"
        got = {}
        cur = None
        for ln in open(lst, errors="replace"):
            m = re.match(r"^\s*[0-9A-F]+:([0-9A-F]{8}) ([0-9A-F]+) *(?:\t\s*(\d+):)?", ln)
            if not m:
                continue
            if m.group(3):
                cur = lineno.get(int(m.group(3)))
                if cur is not None:
                    got[cur] = m.group(2)
            elif cur is not None:
                got[cur] += m.group(2)
        newbad = set()
        for pc, size, t in insns:
            if pc in bad:
                continue
            if bytes.fromhex(got.get(pc, "")) != d[pc:pc + size]:
                newbad.add(pc)
        if not newbad:
            return bad
        bad |= newbad
    return bad


JMPTBL = {}  # data address -> code offset, from the cdi_hotel_data module


def load_jmptbl(path):
    """cdi_hotel_data carries the program's initialised data: u32 data offset, u32 size, then the bytes.
    Its jump table is a run of `jmp abs.l` (4EF9) entries whose targets are module offsets."""
    if not os.path.exists(path):
        return
    d = open(path, "rb").read()
    off, size = struct.unpack(">II", d[0x44:0x4C])
    blob = d[0x4C:0x4C + size]
    for i in range(0, size - 5, 2):
        if blob[i:i + 2] == b"\x4e\xf9":
            JMPTBL[off + i] = struct.unpack(">I", blob[i + 2:i + 6])[0]


def annotate(t, pc, prev, code_syms, data_syms):
    """Comment text naming globals (a6-relative) and far-call targets (move.l #x,d0 / jsr d8(pc,d0.l))."""
    notes = []
    for m in re.finditer(r"(-?)\$([0-9a-f]+)\(a6\)", t or ""):
        v = (-1 if m.group(1) else 1) * int(m.group(2), 16)
        jt = JMPTBL.get(v + 0x8000)  # a6 points 0x8000 past the start of the data area
        if jt is not None:
            notes.append("-> " + code_syms.get(jt, f"${jt:x}"))
            continue
        name = data_syms.get(v & 0xFFFFFFFF)
        if name:
            notes.append(name)
    m = re.match(r"jsr \$([0-9a-f]+)\(pc,d0\.l\)", t or "")
    if m and prev:
        k = re.match(r"move\.l #\$([0-9a-f]+),d0", prev)
        if k:
            tgt = pc + 2 + int(m.group(1), 16) + int(k.group(1), 16)
            notes.append("-> " + code_syms.get(tgt, f"${tgt:x}"))
    return " ".join(notes)


def emit(d, insns, bad, code_syms, first, idata, asmdir, data_syms=None):
    data_syms = data_syms or {}
    os.makedirs(os.path.join(asmdir, "funcs"), exist_ok=True)
    targets = set()
    for pc, size, t in insns:
        if t and pc not in bad:
            mn = t.split(" ", 1)[0]
            m = re.fullmatch(r"\$([0-9a-f]+)", t.split(" ", 1)[1].split(",")[-1]) if " " in t else None
            if BRANCH.match(mn) and m:
                targets.add(int(m.group(1), 16))
    # only label addresses where a verified instruction starts; others stay as absolute numbers
    targets &= {pc for pc, _, t in insns if t and pc not in bad}

    def label(a):
        return code_syms.get(a, f"loc_{a:x}")

    order = sorted(code_syms)
    files = []
    cur = None
    fh = None
    prev = None
    for pc, size, t in insns:
        note = annotate(t, pc, prev, code_syms, data_syms)
        prev = t
        if pc in code_syms:
            if fh:
                fh.close()
            cur = code_syms[pc]
            files.append(cur)
            fh = open(os.path.join(asmdir, "funcs", cur + ".s"), "w")
            fh.write(f"; {cur} @ ${pc:x}\n{cur}:\n")
        elif fh is None:
            cur = f"_start_{pc:x}"
            files.append(cur)
            fh = open(os.path.join(asmdir, "funcs", cur + ".s"), "w")
        elif pc in targets:
            fh.write(f"{label(pc)}:\n")
        if pc in bad or t is None:
            words = ",".join(f"${d[a]:02x}{d[a + 1]:02x}" for a in range(pc, pc + size, 2))
            fh.write(f"    dc.w {words:<36} ; {pc:06x} {t or '??'}\n")
        else:
            mn, _, ops = t.partition(" ")
            if BRANCH.match(mn):
                ops = re.sub(r"\$([0-9a-f]+)$", lambda m: label(int(m.group(1), 16))
                             if int(m.group(1), 16) in targets else m.group(0), ops)
            fh.write(f"    {mn:<8} {ops:<36} ; {pc:06x}{' ' + note if note else ''}\n")
    fh.close()

    with open(os.path.join(asmdir, "cdi_hotel.s"), "w") as m:
        m.write("; cdi_hotel - OS-9/68000 program module (Hotel Mario, Europe)\n")
        m.write(" org 0\n")
        m.write(' incbin "build/eu/split/header.bin"\n')
        for f in files:
            m.write(f' include "asm/funcs/{f}.s"\n')
        m.write(' incbin "build/eu/split/data.bin"\n')
    return files


def main():
    base, stbp, asmdir = sys.argv[1:4]
    d = open(base, "rb").read()
    idata = struct.unpack(">I", d[0x40:0x44])[0]
    exec_off = struct.unpack(">I", d[0x30:0x34])[0]
    # code starts at the entry point; symbols below it (btext etc.) label the header
    syms = parse(stbp)
    code_syms = {}
    for v, f, n in syms:  # several names can share an address; keep the first
        if f & CODE_FLAG and v >= exec_off:
            code_syms.setdefault(v, n)
    data_syms = {}
    for v, f, n in syms:
        if not f & CODE_FLAG:
            data_syms.setdefault(v, n)
    first = exec_off
    load_jmptbl(os.path.join(os.path.dirname(base), "cdi_hotel_data"))
    insns = decode(d, first, idata, code_syms)
    tmp = os.path.join("build", "tmp")
    os.makedirs(tmp, exist_ok=True)
    bad = verify(d, insns, tmp)
    files = emit(d, insns, bad, code_syms, first, idata, asmdir, data_syms)
    split = sys.argv[4] if len(sys.argv) > 4 else os.path.join("build", "eu", "split")
    os.makedirs(split, exist_ok=True)
    open(os.path.join(split, "header.bin"), "wb").write(d[:first])
    open(os.path.join(split, "data.bin"), "wb").write(d[idata:])
    nbad = sum(s for p, s, t in insns if p in bad)
    print(f"{len(insns)} instructions, {len(files)} files, {len(bad)} raw ({nbad} bytes of {idata - first})")


if __name__ == "__main__":
    main()
