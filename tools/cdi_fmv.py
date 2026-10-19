"""Turn a CD-i real-time file (raw sectors, from your own disc) into an MP4: RL7 animation + ADPCM audio.

Usage: python tools/cdi_fmv.py <file.rtf> <out_dir> [--video-ch 15] [--audio-ch 15]

- RL7 frames are separated by runs of zero bytes (padding); each frame is timed by the sector
  its data ends in (single speed = 75 sectors/s), so picture and sound stay in sync.
- The palette is the latest RGB palette data sector seen on the video channel, else on any channel.
- Audio is CD-i/XA ADPCM (levels B/C, 4-bit, mono or stereo).
Output (frames, wav, mp4) goes to out_dir under the git-ignored build/ folder.
"""
import os
import re
import struct
import subprocess
import sys
import wave

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from cdi_gfx import decode_rl7, looks_like_palette  # noqa: E402

RAW = 2352
SECTORS_PER_SEC = 75
K0 = (0, 60, 115, 98)
K1 = (0, 0, -52, -55)


def read_sectors(path):
    d = open(path, "rb").read()
    for i in range(len(d) // RAW):
        s = d[i * RAW:(i + 1) * RAW]
        sm = s[18]
        yield i, s[17], sm, s[19], s[24:24 + (2324 if sm & 0x20 else 2048)]


# ---------------------------------------------------------------- audio

def adpcm_sector(payload, stereo, hist):
    """Decode one 4-bit ADPCM audio sector (18 sound groups x 128 bytes). hist = [[p1,p2],[p1,p2]]."""
    out = []
    for g in range(18):
        grp = payload[g * 128:(g + 1) * 128]
        params = grp[4:12]
        units = []
        for k in range(8):
            shift, filt = params[k] & 0x0F, (params[k] >> 4) & 0x03
            h = hist[k & 1] if stereo else hist[0]
            samples = []
            for j in range(28):
                byte = grp[16 + j * 4 + (k >> 1)]
                nib = (byte >> 4) if k & 1 else (byte & 0x0F)
                t = nib - 16 if nib & 8 else nib
                s = ((t << 12) >> shift) + ((h[0] * K0[filt] + h[1] * K1[filt] + 32) >> 6)
                s = max(-32768, min(32767, s))
                h[1], h[0] = h[0], s
                samples.append(s)
            units.append(samples)
        if stereo:
            for k in range(0, 8, 2):
                for left, right in zip(units[k], units[k + 1]):
                    out += (left, right)
        else:
            for u in units:
                out += u
    return out


def decode_audio(sectors, ch):
    """Returns (rate, stereo, start_time, samples)."""
    hist = [[0, 0], [0, 0]]
    pcm, rate, stereo, start = [], None, None, None
    for i, c, sm, ci, payload in sectors:
        if c != ch or not sm & 0x04:
            continue
        if ci & 0x30:  # 8-bit audio: not used on this disc so far
            continue
        if rate is None:
            rate = 18900 if ci & 0x04 else 37800
            stereo = bool(ci & 0x01)
            start = i / SECTORS_PER_SEC
        if (18900 if ci & 0x04 else 37800) != rate or bool(ci & 0x01) != stereo:
            continue  # a second audio stream with another format on the same channel
        pcm += adpcm_sector(payload, stereo, hist)
    return rate, stereo, start, pcm


def write_wav(path, rate, stereo, start, pcm):
    channels = 2 if stereo else 1
    lead = [0] * (int(start * rate) * channels)  # silence until the audio's first sector
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack(f"<{len(lead) + len(pcm)}h", *(lead + pcm)))


# ---------------------------------------------------------------- video

def rl7_frames(sectors, ch):
    """Yield (time, palette, frame_bytes) for each RL7 frame on channel ch."""
    buf = bytearray()
    owner = []          # sector index for every 2324-byte payload chunk appended to buf
    palette = {}
    any_pal = None
    for i, c, sm, ci, payload in sectors:
        if sm & 0x08 and c != 0 and looks_like_palette(payload):
            palette[c] = any_pal = payload[:384]
            continue
        if c != ch or not sm & 0x02 or ci & 0x0F != 4:
            continue
        buf += payload
        owner.append((len(buf), i))
        while True:
            m = re.search(rb"\x00{8,}", buf)
            if not m or m.end() == len(buf):
                break
            frame = bytes(buf[:m.start()])
            end_sector = next(s for upto, s in owner if upto >= m.start())
            if frame:
                yield end_sector / SECTORS_PER_SEC, palette.get(ch, any_pal), frame
            cut = m.end()
            del buf[:cut]
            owner = [(upto - cut, s) for upto, s in owner if upto - cut > 0]
    if buf.strip(b"\0"):
        yield owner[-1][1] / SECTORS_PER_SEC if owner else 0, palette.get(ch, any_pal), bytes(buf)


def main():
    path, outdir = sys.argv[1], sys.argv[2]
    vch = int(sys.argv[sys.argv.index("--video-ch") + 1]) if "--video-ch" in sys.argv else 15
    ach = int(sys.argv[sys.argv.index("--audio-ch") + 1]) if "--audio-ch" in sys.argv else vch
    os.makedirs(os.path.join(outdir, "frames"), exist_ok=True)
    sectors = list(read_sectors(path))

    times = []
    for n, (t, pal, frame) in enumerate(rl7_frames(sectors, vch)):
        img = decode_rl7(frame, raw=pal is not None)
        if pal is not None:
            img = img.convert("P")
            img.putpalette(list(pal) + [0] * (768 - len(pal)))
        img.convert("RGB").save(os.path.join(outdir, "frames", f"{n:05d}.png"))
        times.append(t)
    print(f"{len(times)} frames, {times[0]:.2f}s .. {times[-1]:.2f}s")

    rate, stereo, start, pcm = decode_audio(sectors, ach)
    wav = os.path.join(outdir, "audio.wav")
    if pcm:
        write_wav(wav, rate, stereo, start, pcm)
        print(f"audio ch{ach}: {rate} Hz {'stereo' if stereo else 'mono'}, starts {start:.2f}s, "
              f"{len(pcm) / (2 if stereo else 1) / rate:.1f}s long")

    # ffmpeg concat list: show each frame until the next one's time
    lst = os.path.join(outdir, "frames.txt")
    with open(lst, "w") as f:
        f.write(f"file 'frames/{0:05d}.png'\nduration {times[0]:.4f}\n")  # hold first frame from 0s
        for n in range(len(times)):
            dur = (times[n + 1] - times[n]) if n + 1 < len(times) else 0.5
            f.write(f"file 'frames/{n:05d}.png'\nduration {max(dur, 0.001):.4f}\n")
        f.write(f"file 'frames/{len(times) - 1:05d}.png'\n")
    mp4 = os.path.join(outdir, os.path.splitext(os.path.basename(path))[0] + ".mp4")
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst]
    if pcm:
        cmd += ["-i", wav, "-c:a", "aac", "-b:a", "192k"]
    cmd += ["-vf", "scale=768:-2:flags=neighbor,fps=25", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-shortest", mp4]
    subprocess.run(cmd, check=True)
    print("wrote", mp4)


if __name__ == "__main__":
    main()
