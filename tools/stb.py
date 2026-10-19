"""Parse a Microware OS-9 .stb debugger symbol module.

Layout (big endian), found from cdi_hotel.stb:
  0x00  standard OS-9 module header (sync 4AFC), name at M$Name
  0x3E  u16 version?  u32 crc/stamp of the program module
  0x44  u32 offset of symbol records
  0x48  u32 number of records
  record: u32 value, u16 flags, u32 offset of NUL-terminated name
"""
import struct
import sys


def parse(path):
    s = open(path, "rb").read()
    off, count = struct.unpack(">II", s[0x44:0x4C])
    syms = []
    for i in range(count):
        val, flags, noff = struct.unpack(">IHI", s[off + i * 10:off + i * 10 + 10])
        name = s[noff:s.index(b"\0", noff)].decode("latin1")
        syms.append((val, flags, name))
    return syms


if __name__ == "__main__":
    for val, flags, name in parse(sys.argv[1]):
        print(f"{val:08x} {flags:04x} {name}")
