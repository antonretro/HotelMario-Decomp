"""Build the PC port: recompile cdi_hotel from your disc, then compile it with the runtime.

Usage: python tools/build_port.py [exe name]   -> build/port/hotel.exe (or the given name)
"""
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GCC = os.environ.get("CC", r"C:\mingw64\bin\gcc.exe")
OUT = os.path.join(ROOT, "build", "port")


MUSASHI = os.path.join(ROOT, "tools", "ext", "Musashi")
SDL = os.path.join(ROOT, "tools", "ext", "SDL2-2.32.10", "x86_64-w64-mingw32")


def musashi_sources():
    """Musashi 68000 core (MIT, Karl Stenerud) from tools/ext/Musashi; generate its opcode table once."""
    if not os.path.exists(os.path.join(MUSASHI, "m68kcpu.c")):
        sys.exit("Musashi missing: git clone --depth 1 https://github.com/kstenerud/Musashi.git tools/ext/Musashi")
    if not os.path.exists(os.path.join(MUSASHI, "m68kops.c")):
        mk = os.path.join(MUSASHI, "m68kmake.exe")
        subprocess.run([GCC, "-O1", os.path.join(MUSASHI, "m68kmake.c"), "-o", mk], check=True)
        subprocess.run([mk, MUSASHI, os.path.join(MUSASHI, "m68k_in.c")], check=True)
    return [os.path.join(MUSASHI, f) for f in ("m68kcpu.c", "m68kops.c", "m68kdasm.c",
                                                os.path.join("softfloat", "softfloat.c"))]


def main():
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "recomp.py")], check=True, cwd=ROOT)
    os.makedirs(OUT, exist_ok=True)
    srcs = sorted(glob.glob(os.path.join(ROOT, "build", "recomp", "*.c"))) + \
        sorted(glob.glob(os.path.join(ROOT, "port", "*.c"))) + musashi_sources()
    headers = [os.path.join(ROOT, "port", h) for h in ("m68k.h", "runtime.h", "musashi_conf.h", "settings.h")]
    if not os.path.isdir(SDL):
        sys.exit("SDL2 missing: extract SDL2-devel-2.32.10-mingw.zip (libsdl-org/SDL releases) into tools/ext")
    flags = ["-O1", "-w", "-I" + os.path.join(ROOT, "port"), "-I" + MUSASHI, '-DMUSASHI_CNF="musashi_conf.h"',
             "-I" + os.path.join(SDL, "include", "SDL2")]
    objs = []
    for s in srcs:
        o = os.path.join(OUT, os.path.basename(s)[:-2] + ".o")
        if not os.path.exists(o) or os.path.getmtime(o) < max([os.path.getmtime(s)] + [os.path.getmtime(h) for h in headers]):
            subprocess.run([GCC, "-c"] + flags + [s, "-o", o], check=True)
        objs.append(o)
    exe = os.path.join(OUT, sys.argv[1] if len(sys.argv) > 1 else "hotel.exe")  # e.g. hotel_dev.exe while one runs
    subprocess.run([GCC] + objs + ["-L" + os.path.join(SDL, "lib"), "-lSDL2", "-lole32", "-lshell32", "-o", exe], check=True)
    try:
        shutil.copy(os.path.join(SDL, "bin", "SDL2.dll"), OUT)
    except PermissionError:  # a running copy of the game has it open; it is already there
        pass
    print("built", exe)


if __name__ == "__main__":
    main()
