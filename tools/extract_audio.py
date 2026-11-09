"""Extract Hotel Mario's audio from the disc's real-time files as clean, separate clips.

Usage: python -I tools/extract_audio.py [--fs build/eu/fs] [--out extracted_audio] [--mp3] [--split-pauses]

Every audio sector of a real-time file carries a channel number and a coding byte (level B 37.8 kHz or
level C 18.9 kHz, mono or stereo). Each channel/coding pair is its own stream, and the end-of-record flag
ends a piece, so every piece comes out on its own, before the game mixes anything:

  voices/  level C mono streams: the cutscene soundtracks (intro, ending, each hotel's opening and
           closing cutscene). Speech is mastered into these with whatever music and effects the cartoon
           has, so lines are clean only where the cutscene itself is quiet behind them. Split them into
           named lines with tools/name_voice_lines.py, or at the pauses with --split-pauses (voices/pauses/).
  music/   level B stereo streams: title music, the four tracks of each hotel, the cutscene music.
  sfx/     level B mono sectors in LN_av.rtf: one short blip, the same in every record of every hotel.
           The 16 stage effects (playAFX) are loaded into RAM from elsewhere and aren't found yet.

Decoding is port/audio.c's ADPCM decoder (tools/cdi_adpcm.c, compiled on first use with gcc; a slow
pure-Python copy is used when there's no compiler). --mp3 encodes music with ffmpeg (192 kbps) to save
space. The output is game audio from your own disc: it's git-ignored, never commit or share it.
"""
import argparse
import ctypes
import hashlib
import os
import shutil
import subprocess
import sys
import wave
from array import array

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = 2352
GROUPS = 2304
HOTELS = {1: "morton", 2: "roy", 3: "larry", 4: "lemmy", 5: "ludwig", 7: "wendy", 8: "bowser"}

# ---------------------------------------------------------------- decoder

_lib = None


def _load_lib():
    global _lib
    if _lib is not None:
        return _lib or None
    src = os.path.join(ROOT, "tools", "cdi_adpcm.c")
    out_dir = os.path.join(ROOT, "build", "tmp")
    os.makedirs(out_dir, exist_ok=True)
    dll = os.path.join(out_dir, "cdi_adpcm" + (".dll" if os.name == "nt" else ".so"))
    gcc = os.environ.get("CC") or (r"C:\mingw64\bin\gcc.exe" if os.name == "nt" else "gcc")
    try:
        if not os.path.exists(dll) or os.path.getmtime(dll) < os.path.getmtime(src):
            subprocess.run([gcc, "-O2", "-shared", "-static", src, "-o", dll], check=True)
        _lib = ctypes.CDLL(dll)
        _lib.decode_sectors.restype = ctypes.c_int
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"no compiled decoder ({e}); using the slow Python one", file=sys.stderr)
        _lib = False
        return None
    return _lib


K0 = (0, 60, 115, 98)
K1 = (0, 0, -52, -55)


def _decode_py(data, stereo):
    hist = [[0, 0], [0, 0]]
    out = array("h")
    for off in range(0, len(data), GROUPS):
        for g in range(18):
            grp = data[off + g * 128: off + g * 128 + 128]
            units = []
            for k in range(8):
                shift, filt = grp[4 + k] & 15, (grp[4 + k] >> 4) & 3
                h = hist[k & 1 if stereo else 0]
                u = []
                for j in range(28):
                    nib = (grp[16 + j * 4 + (k >> 1)] >> (4 if k & 1 else 0)) & 15
                    t = nib - 16 if nib & 8 else nib
                    s = ((t << 12) >> shift) + ((h[0] * K0[filt] + h[1] * K1[filt] + 32) >> 6)
                    s = -32768 if s < -32768 else 32767 if s > 32767 else s
                    h[1], h[0] = h[0], s
                    u.append(s)
                units.append(u)
            if stereo:
                for k in range(0, 8, 2):
                    for j in range(28):
                        out.append(units[k][j])
                        out.append(units[k + 1][j])
            else:
                for u in units:
                    out.extend(u)
    return out


def decode(data, stereo):
    """data: sound groups of consecutive sectors of one stream (2304 bytes each). Returns int16 samples."""
    lib = _load_lib()
    if not lib:
        return _decode_py(data, stereo)
    count = len(data) // GROUPS
    buf = (ctypes.c_int16 * (count * 18 * 8 * 28))()
    hist = (ctypes.c_int * 4)()
    n = lib.decode_sectors(data, count, int(stereo), buf, hist)
    out = array("h")
    out.frombytes(bytes(buf)[: n * 2])
    return out


# ---------------------------------------------------------------- disc streams


def pieces(path):
    """Yield (channel, coding, first_sector, last_sector, groups) for each audio piece of a real-time file:
    a run of one channel/coding stream up to its end-of-record (or end-of-file) flag."""
    open_runs = {}
    with open(path, "rb") as f:
        i = 0
        while True:
            s = f.read(RAW)
            if len(s) < RAW:
                break
            ch, sm, ci = s[17], s[18], s[19]
            if sm & 0x04 and not ci & 0x30:  # audio, 4-bit ADPCM
                key = (ch, ci & 0x05)
                run = open_runs.setdefault(key, [i, i, bytearray()])
                run[1] = i
                run[2] += s[24:24 + GROUPS]
                if sm & 0x81:  # end of record / end of file
                    yield key[0], key[1], run[0], run[1], bytes(run[2])
                    del open_runs[key]
            i += 1
    for (ch, ci), (a, b, g) in sorted(open_runs.items(), key=lambda kv: kv[1][0]):
        yield ch, ci, a, b, bytes(g)


def write_wav(path, samples, rate, channels):
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        if sys.byteorder == "big":
            samples = array("h", samples)
            samples.byteswap()
        w.writeframes(samples.tobytes())


def to_mp3(wav_path):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        return wav_path
    mp3 = wav_path[:-4] + ".mp3"
    subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", wav_path, "-b:a", "192k", mp3], check=True)
    os.remove(wav_path)
    return mp3


# ---------------------------------------------------------------- voice line splitting


def split_lines(samples, rate, min_gap=0.40, frame=0.02, pad=0.10, min_len=0.25):
    """Cut mono speech at pauses. Returns [(start, end)] in samples. A frame is quiet when its RMS is under
    an adaptive threshold (a level well above the stream's noise floor)."""
    n = int(rate * frame)
    rms = []
    for i in range(0, len(samples) - n + 1, n):
        seg = samples[i:i + n]
        rms.append((sum(v * v for v in seg) / n) ** 0.5)
    if not rms:
        return []
    srt = sorted(rms)
    floor = srt[len(srt) // 10]
    loud = srt[int(len(srt) * 0.9)]
    thr = max(floor * 3, loud * 0.04, 60)
    voiced = [r > thr for r in rms]
    gap = int(min_gap / frame)
    lines, start, quiet = [], None, 0
    for i, v in enumerate(voiced + [False] * (gap + 1)):
        if v:
            if start is None:
                start = i
            quiet = 0
        elif start is not None:
            quiet += 1
            if quiet > gap:
                end = i - quiet + 1
                if (end - start) * frame >= min_len:
                    lines.append((start, end))
                start, quiet = None, 0
    p = int(pad / frame)
    return [(max(0, a - p) * n, min(len(rms), b + p) * n) for a, b in lines]


# ---------------------------------------------------------------- main


def describe(fname, ch, ci, idx):
    base = fname[:-4]
    stereo, level_c = ci & 1, ci & 4
    if base.startswith("L") and base[1].isdigit():
        hotel = int(base[1])
        base = f"hotel{hotel}_{HOTELS.get(hotel, 'unknown')}"
    if level_c and not stereo:
        kind = "voices"
        if fname == "intro.rtf":
            name = "intro_cartoon"
        elif fname == "exit.rtf":
            name = f"ending_{idx + 1}"
        else:
            name = f"{base}_{'opening' if idx == 0 else 'closing' if idx == 1 else idx + 1}_cutscene"
    elif stereo:
        kind = "music"
        if fname == "bumper.rtf":
            name = "philips_logo_jingle"
        elif fname == "intro.rtf" and ch == 15:
            name = "title_theme"
        elif ch == 15 and fname.endswith("_am.rtf"):
            name = f"{base}_ch15"
        else:
            name = f"{base}_ch{ch}"
        if idx:
            name += f"_{idx + 1}"
    else:
        kind = "sfx"
        name = "animation_blip" if fname.endswith("_av.rtf") else f"{base}_ch{ch}_{idx + 1:02d}"
    return kind, name


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fs", default=os.path.join(ROOT, "build", "eu", "fs"), help="extracted disc files")
    ap.add_argument("--out", default=os.path.join(ROOT, "extracted_audio"))
    ap.add_argument("--mp3", action="store_true", help="encode music as 192 kbps MP3 (needs ffmpeg)")
    ap.add_argument("--split-pauses", action="store_true", help="also cut cutscene speech at pauses")
    ap.add_argument("files", nargs="*", help="only these real-time files (default: all)")
    a = ap.parse_args()

    files = a.files or sorted(f for f in os.listdir(a.fs) if f.endswith(".rtf"))
    for d in ("voices", "music", "sfx") + (("voices/pauses",) if a.split_pauses else ()):
        os.makedirs(os.path.join(a.out, d), exist_ok=True)
    seen = {}
    report = []
    for fname in files:
        counts = {}
        for ch, ci, first, last, groups in pieces(os.path.join(a.fs, fname)):
            key = (ch, ci)
            idx = counts.get(key, 0)
            counts[key] = idx + 1
            kind, name = describe(fname, ch, ci, idx)
            digest = hashlib.sha1(groups).hexdigest()
            if digest in seen:
                report.append(f"{kind}/{name}: same as {seen[digest]}, skipped")
                continue
            seen[digest] = f"{kind}/{name}"
            stereo = bool(ci & 1)
            rate = 18900 if ci & 4 else 37800
            pcm = decode(groups, stereo)
            secs = len(pcm) / rate / (2 if stereo else 1)
            path = os.path.join(a.out, kind, name + ".wav")
            write_wav(path, pcm, rate, 2 if stereo else 1)
            if kind == "music" and a.mp3:
                path = to_mp3(path)
            report.append(f"{kind}/{os.path.basename(path)}: {fname} ch{ch} sectors {first}-{last}, {secs:.1f} s")
            if kind == "voices" and a.split_pauses:
                for n, (s, e) in enumerate(split_lines(pcm, rate), 1):
                    write_wav(os.path.join(a.out, "voices", "pauses", f"{name}_{n:02d}.wav"), pcm[s:e], rate, 1)
                    report.append(f"  pauses/{name}_{n:02d}.wav: {s / rate:.2f}-{e / rate:.2f} s")
            print(report[-1] if not report[-1].startswith("  ") else f"{name}: split", flush=True)
    with open(os.path.join(a.out, "index.txt"), "w") as f:
        f.write("\n".join(report) + "\n")


if __name__ == "__main__":
    main()
