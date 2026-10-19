"""Summarise the sectors of a CD-i real-time file extracted by cdi_fs.py (raw 2352-byte sectors).

Usage: python tools/rtf_scan.py <file.rtf> [more.rtf ...]
Prints, per channel, how many video/audio/data sectors and records it has, and the video coding formats.
"""
import collections
import sys

RAW = 2352
FORMATS = {0: "CLUT4", 1: "CLUT7", 2: "CLUT8", 3: "RL3", 4: "RL7", 5: "DYUV", 6: "RGB555L", 7: "RGB555U", 8: "QHY"}
RES = {0: "normal", 1: "double", 3: "high"}


def sectors(path):
    d = open(path, "rb").read()
    for i in range(len(d) // RAW):
        yield d[i * RAW:(i + 1) * RAW]


def describe_video(coding):
    return f"{FORMATS.get(coding & 0x0F, hex(coding & 0x0F))}/{RES.get((coding >> 4) & 3, '?')}"


def main():
    for path in sys.argv[1:]:
        stats = collections.Counter()
        records = collections.Counter()
        for s in sectors(path):
            ch, sm, ci = s[17], s[18], s[19]
            kind = "V" if sm & 0x02 else "A" if sm & 0x04 else "D"
            key = (ch, kind, describe_video(ci) if kind == "V" else f"{ci:02x}")
            stats[key] += 1
            if sm & 0x01:  # end of record
                records[key] += 1
        print(path)
        for (ch, kind, ci), n in sorted(stats.items()):
            print(f"  ch{ch:<2} {kind} {ci:<14} sectors={n:<6} records={records[(ch, kind, ci)]}")


if __name__ == "__main__":
    main()
