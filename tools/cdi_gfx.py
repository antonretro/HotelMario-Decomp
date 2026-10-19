"""Decode CD-i video sectors from a real-time file (raw 2352-byte sectors) into PNGs.

Usage: python tools/cdi_gfx.py <file.rtf> <out_dir> [--channel N]

Video sectors are grouped into images per channel; an image ends at a sector with the End-Of-Record bit.
Supported so far: DYUV (Green Book delta-YUV, needs no palette). CLUT/RL7 images are written as
grey-scale previews until their palettes are found.
Everything is read from the user's own disc image; nothing is stored in the repository.
"""
import os
import sys

from PIL import Image

RAW = 2352
WIDTH = 384  # normal resolution line length

# Green Book DYUV delta quantisation table
DQ = (0, 1, 4, 9, 16, 27, 44, 79, 128, 177, 212, 229, 240, 247, 252, 255)


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else int(v)


def yuv_to_rgb(y, u, v):
    u -= 128
    v -= 128
    return clamp(y + 1.371 * v), clamp(y - 0.336 * u - 0.698 * v), clamp(y + 1.733 * u)


def add_delta(value, nibble):
    """Table entries >= 128 are negative steps; results are clamped to 0..255."""
    d = DQ[nibble]
    return clamp(value + (d - 256 if d >= 128 else d))


def decode_dyuv(data, width=WIDTH, start=(16, 128, 128)):
    """Each 2 bytes = 2 pixels: (dY0<<4 | dU), (dY1<<4 | dV). Values restart at `start` on every line.
    The nibble order was picked by measuring which layout gives the smoothest luminance on this disc."""
    height = len(data) // width
    img = Image.new("RGB", (width, height))
    px = img.load()
    for row in range(height):
        y, u, v = start
        line = data[row * width:(row + 1) * width]
        for x in range(0, width - 1, 2):
            b0, b1 = line[x], line[x + 1]
            y0 = add_delta(y, b0 >> 4)
            u = add_delta(u, b0 & 0xF)
            y = add_delta(y0, b1 >> 4)
            v = add_delta(v, b1 & 0xF)
            px[x, row] = yuv_to_rgb(y0, u, v)
            px[x + 1, row] = yuv_to_rgb(y, u, v)
    return img


def decode_grey(data, bits, width=WIDTH, raw=False):
    """Preview for palette images: index -> grey (raw=True keeps the plain colour indices)."""
    if bits == 4:
        data = bytes(n for b in data for n in (b >> 4, b & 0xF))
        scale = 17
    else:
        scale = 2
    height = len(data) // width
    if raw:
        return Image.frombytes("L", (width, height), bytes(b & 0x7F for b in data[:width * height]))
    img = Image.frombytes("L", (width, height), bytes(min(255, (b & 0x7F) * scale) for b in data[:width * height]))
    return img


def decode_rl7(data, width=WIDTH, raw=False):
    """RL7: byte with bit 7 clear = one pixel of colour b; bit 7 set = colour (b & 0x7f), next byte = run length
    (0 = to end of line). Returns (indices, height)."""
    out = bytearray()
    lines = 0
    i = 0
    x = 0
    while i < len(data):
        b = data[i]
        i += 1
        if b & 0x80:
            if i >= len(data):
                break
            n = data[i]
            i += 1
            if n == 0:
                n = width - x
            out += bytes([b & 0x7F]) * n
            x += n
        else:
            out.append(b)
            x += 1
        if x >= width:
            lines += 1
            x = 0
    height = len(out) // width
    if raw:
        return Image.frombytes("L", (width, height), bytes(out[:width * height]))
    img = Image.frombytes("L", (width, height), bytes(min(255, c * 2) for c in out[:width * height]))
    return img


def records(path, channel=None, with_palettes=False):
    """Yield (channel, coding, bytes) for every video record in the file.
    With with_palettes, also yields ('pal', channel, rgb_bytes) when a data sector holds an RGB palette."""
    d = open(path, "rb").read()
    cur = {}
    prev_data = {}  # channel -> was the previous sector on it a data sector?
    for i in range(len(d) // RAW):
        s = d[i * RAW:(i + 1) * RAW]
        ch, sm, ci = s[17], s[18], s[19]
        if channel is not None and ch != channel:
            continue
        payload = s[24:24 + (2324 if sm & 0x20 else 2048)]
        is_data = bool(sm & 0x08)  # submode: 0x02 video, 0x04 audio, 0x08 data
        first_of_run = is_data and not prev_data.get(ch)
        prev_data[ch] = is_data
        if first_of_run and with_palettes and ch != 0 and looks_like_palette(payload):
            yield "pal", ch, payload[:128 * 3]
            continue
        if not sm & 0x02:
            continue
        key = (ch, ci)
        cur.setdefault(key, bytearray()).extend(payload)
        if sm & 0x01:  # end of record
            yield ch, ci, bytes(cur.pop(key))
    for (ch, ci), buf in cur.items():  # trailing data without EOR
        yield ch, ci, bytes(buf)


def looks_like_palette(p):
    """First data sector of a run next to CLUT video: 128 RGB triplets, not an OS-9 module (4AFC)."""
    return p[:2] != b"\x4a\xfc" and len(set(p[:384])) > 16


def apply_palette(indices_img, pal):
    img = indices_img.convert("P")
    img.putpalette(list(pal) + [0] * (768 - len(pal)))
    return img.convert("RGB")


def main():
    path, outdir = sys.argv[1], sys.argv[2]
    channel = int(sys.argv[sys.argv.index("--channel") + 1]) if "--channel" in sys.argv else None
    os.makedirs(outdir, exist_ok=True)
    base = os.path.splitext(os.path.basename(path))[0]
    palettes = {}
    n = 0
    for ch, ci, data in records(path, channel, with_palettes=True):
        if ch == "pal":
            palettes[ci] = data
            continue
        n += 1
        fmt = ci & 0x0F
        pal = palettes.get(ch)
        if fmt == 5:
            img, kind = decode_dyuv(data), "dyuv"
        elif fmt == 4:
            img, kind = decode_rl7(data, raw=pal is not None), "rl7"
        elif fmt in (1, 2):
            img, kind = decode_grey(data, 8, raw=pal is not None), "clut7"
        elif fmt == 0:
            img, kind = decode_grey(data, 4), "clut4"
        else:
            continue
        if pal is not None and kind in ("rl7", "clut7"):
            img, kind = apply_palette(img, pal), kind + "_pal"
        name = f"{base}_{n:03d}_ch{ch}_{kind}.png"
        img.save(os.path.join(outdir, name))
        print(name, img.size, len(data), "bytes")


if __name__ == "__main__":
    main()
