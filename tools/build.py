"""Build cdi_hotel from asm/ and check it against the original.

Usage: python tools/build.py
"""
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from os9mod import fix  # noqa: E402

VASM = os.path.join(HERE, "ext", "vasm", "vasmm68k_mot")
EXPECTED_SHA1 = "c3a10fbf0d2b6c3ef7be2f0f43d8f2c0c1e8c0b1"  # placeholder, set by `build.py --bless` on first match
BASEROM = os.path.join("build", "eu", "fs", "cdi_hotel")
OUT = os.path.join("build", "eu", "cdi_hotel")


def sha1(b):
    return hashlib.sha1(b).hexdigest()


def main():
    r = subprocess.run([VASM, "-quiet", "-no-opt", "-m68000", "-Fbin", "-o", OUT, os.path.join("asm", "cdi_hotel.s")],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stdout + r.stderr)
    built = fix(open(OUT, "rb").read())
    open(OUT, "wb").write(built)
    want = open(BASEROM, "rb").read()
    if built == want:
        print(f"cdi_hotel: OK  sha1 {sha1(built)}")
        return
    n = min(len(built), len(want))
    diff = next((i for i in range(n) if built[i] != want[i]), n)
    sys.exit(f"cdi_hotel: MISMATCH (size {len(built)} vs {len(want)}), first diff at 0x{diff:x}")


if __name__ == "__main__":
    main()
