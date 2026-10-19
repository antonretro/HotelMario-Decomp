"""OS-9/68000 module helpers: header parity and module CRC.

Usage: python tools/os9mod.py check <module>   (recomputes parity + CRC and compares)
       python tools/os9mod.py fix <module>     (rewrites parity + CRC in place)
"""
import struct
import sys


def header_parity(d):
    p = 0
    for i in range(0, 0x2E, 2):
        p ^= struct.unpack(">H", d[i:i + 2])[0]
    return (~p) & 0xFFFF


def crc24(data):
    """OS-9 F$CRC: CRC-24, poly 0x800063, init 0xFFFFFF, MSB first."""
    crc = 0xFFFFFF
    for b in data:
        crc ^= b << 16
        for _ in range(8):
            crc <<= 1
            if crc & 0x1000000:
                crc ^= 0x800063
        crc &= 0xFFFFFF
    return crc


def module_crc(d):
    return (~crc24(d[:-3])) & 0xFFFFFF


def fix(d):
    """Rewrite parity + CRC; bytes past M$Size (disc sector padding) are kept as-is."""
    d = bytearray(d)
    size = struct.unpack(">I", d[4:8])[0]
    d[0x2E:0x30] = struct.pack(">H", header_parity(d))
    d[size - 3:size] = module_crc(d[:size]).to_bytes(3, "big")
    return bytes(d)


if __name__ == "__main__":
    cmd, path = sys.argv[1:3]
    d = open(path, "rb").read()
    size = struct.unpack(">I", d[4:8])[0]
    d = d[:size]
    if cmd == "check":
        hp, cp = struct.unpack(">H", d[0x2E:0x30])[0], int.from_bytes(d[-3:], "big")
        print(f"parity stored {hp:04x} calc {header_parity(d):04x}; crc stored {cp:06x} calc {module_crc(d):06x}")
    elif cmd == "fix":
        open(path, "wb").write(fix(d))
