"""Bring your own disc: verify your Hotel Mario CD-i image, extract it and generate asm/.

Usage: python -I tools/setup.py "<path to your .bin>"

Nothing from the game is stored in this repository; everything is generated into build/ and asm/
(both git-ignored) from the image you supply.
"""
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

KNOWN = {
    "8d671f7bc729ed8d6a07d1d40ba48cbc9530bd30": ("eu", "Hotel Mario (Europe)", True),
    "183b2efd1f5b926757a8fb514cc861fdf7a244c5": ("proto", "Hotel Mario (USA) (Beta) (1993-11-23)", False),
}


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run(*args):
    subprocess.run([sys.executable, "-I", *args], check=True)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    img = sys.argv[1]
    print("checking", img)
    digest = sha1_file(img)
    if digest not in KNOWN:
        sys.exit(f"unknown image (sha1 {digest}). Supported: raw 2352-byte/sector .bin of Hotel Mario (Europe).")
    region, title, supported = KNOWN[digest]
    print("found", title)
    if not supported:
        sys.exit("this version is recognised but not supported by the build yet; use Hotel Mario (Europe).")
    fs = os.path.join("build", region, "fs")
    run(os.path.join(HERE, "cdi_fs.py"), "extract", img, fs)
    mod, stb = os.path.join(fs, "cdi_hotel"), os.path.join(fs, "cdi_hotel.stb")
    run(os.path.join(HERE, "gen_asm.py"), mod, stb, "asm")
    run(os.path.join(HERE, "build.py"))


if __name__ == "__main__":
    main()
