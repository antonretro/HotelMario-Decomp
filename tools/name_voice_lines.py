"""Split the cutscene speech from extract_audio.py into one file per spoken line, named after what is said.

Usage: python -I tools/name_voice_lines.py [--out extracted_audio] [--model large-v3-turbo]

Needs faster-whisper (pip install faster-whisper) and a Whisper model (downloaded on first use, or already in
the Hugging Face cache). Each voices/<cutscene>.wav is transcribed with timestamps; every line becomes
voices/lines/<cutscene>_NN_<first_words>.wav, replacing extract_audio.py's pause-based split of that
cutscene, and voices/lines.txt lists what each file says. Transcripts are machine-made: check them by ear.
"""
import argparse
import glob
import os
import re
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HALLUCINATION = re.compile(r"watching this video|enjoyed this video|see you (in the )?next|subscribe", re.I)


def slug(text, words=7):
    w = re.sub(r"[^a-z0-9' -]", " ", text.lower()).replace("'", "").split()
    return "_".join(w[:words]) or "unclear"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=os.path.join(ROOT, "extracted_audio"))
    ap.add_argument("--model", default="mobiuslabsgmbh/faster-whisper-large-v3-turbo")
    ap.add_argument("--pad", type=float, default=0.12, help="seconds kept before and after each line")
    a = ap.parse_args()

    from faster_whisper import WhisperModel
    model_path = a.model
    if not os.path.isdir(model_path):  # use the Hugging Face cache directly if it's there (no hub import)
        hub = os.environ.get("HF_HUB_CACHE") or os.path.expanduser("~/.cache/huggingface/hub")
        cache = os.path.join(hub, "models--" + model_path.replace("/", "--"))
        snaps = sorted(glob.glob(os.path.join(cache, "snapshots", "*")))
        if snaps:
            model_path = snaps[-1]
    model = WhisperModel(model_path, device="cpu", compute_type="int8")
    vdir = os.path.join(a.out, "voices")
    ldir = os.path.join(vdir, "lines")
    os.makedirs(ldir, exist_ok=True)
    listing = []
    for path in sorted(glob.glob(os.path.join(vdir, "*.wav"))):
        name = os.path.basename(path)[:-4]
        with wave.open(path, "rb") as w:
            rate, frames, params = w.getframerate(), w.readframes(w.getnframes()), w.getparams()
        # hand Whisper 16 kHz float samples directly (mono 16-bit WAVs from extract_audio.py)
        pcm = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
        t = np.arange(0, len(pcm) / rate, 1 / 16000)
        audio = np.interp(t, np.arange(len(pcm)) / rate, pcm).astype(np.float32)
        segs, _ = model.transcribe(audio, language="en", beam_size=5, condition_on_previous_text=False)
        # drop empty lines and Whisper's stock YouTube hallucinations over music
        segs = [s for s in segs if s.text.strip() and not HALLUCINATION.search(s.text)]
        width = params.sampwidth * params.nchannels
        for old in glob.glob(os.path.join(ldir, f"{name}_*.wav")):
            os.remove(old)
        listing.append(f"== {name}.wav")
        for n, s in enumerate(segs, 1):
            a0 = max(0, int((s.start - a.pad) * rate))
            a1 = min(len(frames) // width, int((s.end + a.pad) * rate))
            fn = f"{name}_{n:02d}_{slug(s.text)}.wav"
            with wave.open(os.path.join(ldir, fn), "wb") as o:
                o.setparams(params)
                o.writeframes(frames[a0 * width:a1 * width])
            listing.append(f"{fn}  [{s.start:.1f}-{s.end:.1f} s]  {s.text.strip()}")
            print(listing[-1], flush=True)
    with open(os.path.join(vdir, "lines.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(listing) + "\n")


if __name__ == "__main__":
    main()
