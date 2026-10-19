"""CD-i (Green Book) disc image reader: list and extract files from a raw 2352-byte/sector .bin.

Usage:
  python tools/cdi_fs.py list    <image.bin>
  python tools/cdi_fs.py extract <image.bin> <out_dir>   (extracts non-realtime files, plus raw sector dumps)
"""
import os
import struct
import sys

RAW = 2352
SUBHDR = 16  # sync(12) + header(4)


class Disc:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.f.seek(0, 2)
        self.nsect = self.f.tell() // RAW

    def raw(self, lba):
        self.f.seek(lba * RAW)
        return self.f.read(RAW)

    def subheader(self, lba):
        r = self.raw(lba)
        return r[16], r[17], r[18], r[19]  # file, channel, submode, coding

    def data(self, lba):
        """User data of a sector: 2048 (form 1) or 2324 (form 2)."""
        r = self.raw(lba)
        submode = r[18]
        if submode & 0x20:
            return r[24:24 + 2324]
        return r[24:24 + 2048]

    def block(self, lba):
        return self.raw(lba)[24:24 + 2048]


def label(d):
    for lba in range(16, 32):
        b = d.block(lba)
        if b[1:6] == b"CD-I ":
            return lba, {
                "system": b[8:40].decode("latin1").strip(),
                "volume": b[40:72].decode("latin1").strip(),
                "space": struct.unpack(">I", b[84:88])[0],
                "block": struct.unpack(">H", b[130:132])[0],
                "pt_size": struct.unpack(">I", b[136:140])[0],
                "pt_addr": struct.unpack(">I", b[148:152])[0],
                "app": b[574:702].decode("latin1").strip(),
                "publisher": b[318:446].decode("latin1").strip(),
                "preparer": b[446:574].decode("latin1").strip(),
                "created": b[813:829].decode("latin1"),
            }
    raise SystemExit("no CD-I disc label found")


def path_table(d, addr, size):
    buf = b""
    lba = addr
    while len(buf) < size:
        buf += d.block(lba)
        lba += 1
    buf = buf[:size]
    dirs, i = [], 0
    while i < len(buf):
        nlen = buf[i]
        if nlen == 0:
            break
        lbn, parent = struct.unpack(">IH", buf[i + 2:i + 8])
        name = buf[i + 8:i + 8 + nlen].decode("latin1")
        dirs.append((name, lbn, parent))
        i += 8 + nlen + (nlen & 1)
    return dirs


def read_dir(d, lbn):
    entries = []
    first = d.block(lbn)
    total = None
    lba = lbn
    buf = first
    while True:
        i = 0
        while i < 2048:
            rl = buf[i]
            if rl == 0:
                break
            rec = buf[i:i + rl]
            start = struct.unpack(">I", rec[6:10])[0]
            size = struct.unpack(">I", rec[14:18])[0]
            flags = rec[25]
            nlen = rec[32]
            name = rec[33:33 + nlen]
            su = rec[33 + nlen + ((nlen + 1) & 1):]
            attr = struct.unpack(">H", su[4:6])[0] if len(su) >= 6 else 0
            fnum = su[8] if len(su) >= 9 else 0
            entries.append({
                "name": name, "start": start, "size": size, "flags": flags,
                "attr": attr, "fnum": fnum, "is_dir": bool(attr & 0x8000),
            })
            i += rl
        if total is None and entries and entries[0]["name"] == b"\x00":
            total = entries[0]["size"]
        lba += 1
        if total is None or (lba - lbn) * 2048 >= total:
            break
        buf = d.block(lba)
    return [e for e in entries if e["name"] not in (b"\x00", b"\x01")]


def walk(d, lbl):
    dirs = path_table(d, lbl["pt_addr"], lbl["pt_size"])
    paths = {}
    out = []
    for idx, (name, lbn, parent) in enumerate(dirs, 1):
        p = "" if idx == 1 else paths[parent] + "/" + name
        paths[idx] = p
        for e in read_dir(d, lbn):
            if e["is_dir"]:
                continue
            e["path"] = p + "/" + e["name"].decode("latin1")
            out.append(e)
    return out


def sector_stats(d, e):
    nsec = (e["size"] + 2047) // 2048
    kinds = {}
    for lba in range(e["start"], min(e["start"] + nsec, d.nsect)):
        fn, ch, sm, ci = d.subheader(lba)
        k = ("V" if sm & 0x02 else "A" if sm & 0x04 else "D") + ("2" if sm & 0x20 else "1")
        kinds[k] = kinds.get(k, 0) + 1
    return kinds


def main():
    cmd, img = sys.argv[1], sys.argv[2]
    d = Disc(img)
    at, lbl = label(d)
    if cmd == "list":
        print(f"label @ {at}: {lbl}")
        for e in walk(d, lbl):
            st = sector_stats(d, e)
            print(f"{e['path']:<40} lba={e['start']:<7} size={e['size']:<10} attr={e['attr']:04x} {st}")
    elif cmd == "extract":
        outdir = sys.argv[3]
        for e in walk(d, lbl):
            dst = os.path.join(outdir, e["path"].lstrip("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            nsec = (e["size"] + 2047) // 2048
            with open(dst, "wb") as o:
                if e["attr"] & 0x2000 == 0 and all(not (d.subheader(l)[2] & 0x20) for l in range(e["start"], e["start"] + nsec)):
                    buf = b"".join(d.block(l) for l in range(e["start"], e["start"] + nsec))
                    o.write(buf[:e["size"]])
                else:  # real-time / mixed: keep raw sectors for later demuxing
                    for l in range(e["start"], e["start"] + nsec):
                        o.write(d.raw(l))
            print("wrote", dst)


if __name__ == "__main__":
    main()
