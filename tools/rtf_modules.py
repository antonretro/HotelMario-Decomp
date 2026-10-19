"""Find the OS-9 modules streamed inside real-time files and extract each unique one.

Usage: python tools/rtf_modules.py [fs_dir] [out_dir]     (defaults build/eu/fs, build/eu/modules)

Data sectors are concatenated per channel in disc order; anything starting with the module sync word
4AFC and a correct header parity is a module. Output: <out_dir>/<name>_<crc>.mod plus index.txt.
"""
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from os9mod import header_parity, module_crc  # noqa: E402

RAW = 2352
TYPES = {1: "program", 2: "subroutine", 3: "multi", 4: "data"}


def channel_streams(path):
    d = open(path, "rb").read()
    streams = {}
    for i in range(len(d) // RAW):
        s = d[i * RAW:(i + 1) * RAW]
        if not s[18] & 0x08:
            continue
        size = 2324 if s[18] & 0x20 else 2048
        streams.setdefault(s[17], bytearray()).extend(s[24:24 + size])
    return streams


def modules_in(buf):
    pos = 0
    while True:
        pos = buf.find(b"\x4a\xfc", pos)
        if pos < 0 or pos + 0x48 > len(buf):
            return
        h = bytes(buf[pos:pos + 0x30])
        size = struct.unpack(">I", h[4:8])[0]
        if header_parity(h + b"\0\0") == struct.unpack(">H", h[0x2E:0x30])[0] and 0x48 <= size <= len(buf) - pos:
            yield pos, bytes(buf[pos:pos + size])
            pos += size
        else:
            pos += 2


def main():
    fs = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "eu", "fs")
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join("build", "eu", "modules")
    os.makedirs(out, exist_ok=True)
    seen = {}
    for path in sorted(glob.glob(os.path.join(fs, "*.rtf"))):
        for ch, buf in channel_streams(path).items():
            for pos, m in modules_in(buf):
                nameoff = struct.unpack(">I", m[0x0C:0x10])[0]
                name = m[nameoff:m.index(b"\0", nameoff)].decode("latin1") if nameoff < len(m) else "?"
                typ = m[0x12]
                crc = m[-3:].hex()
                crc_ok = module_crc(m) == int(crc, 16)
                key = (name, crc)
                if key not in seen:
                    seen[key] = []
                    fn = f"{name}_{crc}.mod"
                    open(os.path.join(out, fn), "wb").write(m)
                seen[key].append(f"{os.path.basename(path)}:ch{ch}@{pos:#x}")
                if len(seen[key]) == 1:
                    print(f"{name:<20} {TYPES.get(typ, typ):<10} {len(m):7} bytes crc {crc} {'ok' if crc_ok else 'BAD'} "
                          f"exec {struct.unpack('>I', m[0x30:0x34])[0]:#x}  first in {seen[key][0]}")
    with open(os.path.join(out, "index.txt"), "w") as f:
        for (name, crc), where in sorted(seen.items()):
            f.write(f"{name}_{crc}.mod {len(where)} " + " ".join(where) + "\n")
    print(f"{len(seen)} unique modules")


if __name__ == "__main__":
    main()
