"""Turn the port's --dump-every frames (build/port/frames/*.rgb, 384x280 RGB) into one PNG contact sheet.

Usage: python tools/contact_sheet.py [out.png] [columns] [_A|_B]
Tiles go left to right in time order (file names are ms); _A / _B shows one plane alone.
"""
import glob
import os
import struct
import sys
import zlib

W, H = 384, 280
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write_png(path, w, h, rgb):
    rows = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    chunk = lambda t, b: struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "port", "timeline.png")
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    suffix = sys.argv[3] if len(sys.argv) > 3 else ""      # "", "_A" or "_B": the frame or one plane
    files = sorted(f for f in glob.glob(os.path.join(ROOT, "build", "port", "frames", "*.rgb"))
                   if os.path.basename(f)[6:-4] == suffix)
    if not files:
        sys.exit("no frames - run the port with --dump-every <ms>")
    tw, th, gap = W // 2, H // 2, 4
    rows = (len(files) + cols - 1) // cols
    sw, sh = cols * (tw + gap), rows * (th + gap)
    sheet = bytearray(sw * sh * 3)
    for n, path in enumerate(files):
        d = open(path, "rb").read()
        ox, oy = (n % cols) * (tw + gap), (n // cols) * (th + gap)
        for y in range(th):
            for x in range(tw):
                s = ((y * 2) * W + x * 2) * 3
                o = ((oy + y) * sw + ox + x) * 3
                sheet[o:o + 3] = d[s:s + 3]
    write_png(out, sw, sh, bytes(sheet))
    print(f"{len(files)} frames ({os.path.basename(files[0])[:-4]}..{os.path.basename(files[-1])[:-4]} ms) -> {out}")


if __name__ == "__main__":
    main()
