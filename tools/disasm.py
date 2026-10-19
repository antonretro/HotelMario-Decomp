"""Linear-sweep disassembly of the cdi_hotel OS-9 module using the original .stb symbols.

Usage: python tools/disasm.py <module> <stb> <out.s>
"""
import struct
import sys

import capstone

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from stb import parse  # noqa: E402

CODE_FLAG = 0x0004


def main():
    mod, stbp, out = sys.argv[1:4]
    d = open(mod, "rb").read()
    exec_off = struct.unpack(">I", d[0x30:0x34])[0]
    idata = struct.unpack(">I", d[0x40:0x44])[0]
    syms = parse(stbp)
    code = {v: n for v, f, n in syms if f & CODE_FLAG}
    funcs = sorted(code)
    md = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)

    first = min(funcs + [exec_off])
    end = idata
    bad = 0
    with open(out, "w") as o:
        o.write(f"; header 0x0-0x{first:x}, code 0x{first:x}-0x{end:x}, idata 0x{idata:x}\n")
        pc = first
        while pc < end:
            if pc in code:
                o.write(f"\n{code[pc]}:\n")
            nxt = min([f for f in funcs if f > pc] + [end])
            insns = list(md.disasm(d[pc:nxt], pc, count=1))
            if not insns:
                o.write(f"    dc.w ${d[pc]:02x}{d[pc + 1]:02x}  ; {pc:06x} ??\n")
                pc += 2
                bad += 1
                continue
            i = insns[0]
            o.write(f"    {i.mnemonic:<8} {i.op_str:<40} ; {pc:06x} {i.bytes.hex()}\n")
            pc += i.size
    print(f"functions with symbols: {len(funcs)}, code bytes: {end - first}, undecodable words: {bad}")


if __name__ == "__main__":
    main()
