"""Make the public release zip: the setup program plus the programs it copies, and no game data or game code.

Usage: python -I tools/make_release.py [version]      -> build/release/HotelMario-<version>-windows.zip

What goes in:
  Hotel Mario setup.exe    the patcher (tools/patcher/patcher.c): checks the player's disc, extracts it
  files/pc/hotel.exe       the PC port built with --release: no translated game code, the game's own program
  files/pc/SDL2.dll        runs on the interpreter, read from the player's disc at start-up
  README.txt

The 3DS and Android programs are left out for now: their builds still have the game's program translated
into them, so they can't be shared publicly.

Before zipping, every file is checked for pieces of the game's program (build/eu/fs/cdi_hotel), so nothing
from the disc can slip in.
"""
import os
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REL = os.path.join(ROOT, "build", "release")

README = """Hotel Mario - BIOS-free port of the 1994 Philips CD-i game
=========================================================

Bring your own disc: nothing from the game is included here. You need a disc image you made from your own
copy of Hotel Mario (Europe), as a raw .bin (2352 bytes a sector, with its .cue).

1. Run "Hotel Mario setup.exe".
2. Pick your disc image. The setup checks it's the right disc (SHA-1) and refuses anything else.
3. Tick the versions to make, and press "Make Hotel Mario".
4. Open the "Hotel Mario (PC)" folder it makes and double-click hotel.exe.

Controls: arrows / stick move, Z or A is button 1, X or B is button 2, F1 settings, F11 fullscreen, Esc quits.

The game's own program runs unchanged, read from your disc, on a 68000 interpreter (Musashi); the CD-i's
operating system, video and sound are rebuilt in the port. Source: https://github.com/antonretro/HotelMario-Decomp

Hotel Mario is (c) Nintendo / Philips. This is a fan project, not affiliated with or endorsed by Nintendo,
Philips or Fantasy Factory. Musashi (c) Karl Stenerud, MIT licence. SDL2, zlib licence.
"""


def run(*args):
    subprocess.run([sys.executable, "-I", *args], check=True, cwd=ROOT)


def has_game_code(path, prog):
    """Any 64-byte piece of the game's program (every 4 KB) inside this file?"""
    data = open(path, "rb").read()
    return any(prog[i:i + 64] in data for i in range(0, len(prog) - 64, 4096) if prog[i:i + 64].strip(b"\0"))


def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "test"
    run(os.path.join(ROOT, "tools", "build_port.py"), "--release")
    run(os.path.join(ROOT, "tools", "build_patcher.py"))
    with open(os.path.join(REL, "README.txt"), "w", newline="\r\n") as f:
        f.write(README)
    files = {
        "Hotel Mario setup.exe": os.path.join(REL, "Hotel Mario setup.exe"),
        "README.txt": os.path.join(REL, "README.txt"),
        "files/pc/hotel.exe": os.path.join(REL, "files", "pc", "hotel.exe"),
        "files/pc/SDL2.dll": os.path.join(REL, "files", "pc", "SDL2.dll"),
    }
    prog_path = os.path.join(ROOT, "build", "eu", "fs", "cdi_hotel")
    if os.path.exists(prog_path):
        prog = open(prog_path, "rb").read()
        for name, path in files.items():
            if has_game_code(path, prog):
                sys.exit(f"STOP: {name} contains part of the game's program; not making the zip")
        print("checked: no piece of cdi_hotel in any file")
    else:
        sys.exit("can't check for game code: build/eu/fs/cdi_hotel is missing (run tools/setup.py first)")
    out = os.path.join(REL, f"HotelMario-{version}-windows.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, path in files.items():
            z.write(path, "Hotel Mario/" + name)
    print("made", out, f"({os.path.getsize(out) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
