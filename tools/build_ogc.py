"""Build the Wii or GameCube port (devkitPPC + libogc): the same recompiled game and runtime as the PC port, with
port/ogc/ in place of the SDL and Windows files. Needs build/recomp (made by tools/build_port.py).

Usage: python tools/build_ogc.py [wii|gc]   -> build/wii/apps/hotelmario/boot.dol (Homebrew Channel layout)
                                               build/gc/HotelMario.dol
For the Wii, copy build/wii/apps to the SD card and your extracted disc files (build/eu/fs) to
sd:/apps/hotelmario/game/.
"""
import concurrent.futures
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DKP = os.environ.get("DEVKITPRO_WIN", r"C:\devkitPro")
GCC = os.path.join(DKP, "devkitPPC", "bin", "powerpc-eabi-gcc.exe")
OGC = os.path.join(DKP, "libogc")
ELF2DOL = os.path.join(DKP, "tools", "bin", "elf2dol.exe")
MUSASHI = os.path.join(ROOT, "tools", "ext", "Musashi")
PC_ONLY = {"window.c", "audio.c", "gamefiles.c"}   # replaced by port/ogc/

MACHINE = {"wii": ["-mrvl", "-DHW_RVL"], "gc": ["-mogc", "-DHW_DOL"]}
LIBS = {"wii": ["-lwiiuse", "-lbte", "-lfat", "-lasnd", "-logc", "-lm"], "gc": ["-lfat", "-lasnd", "-logc", "-lm"]}

META = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<app version="1">
  <name>Hotel Mario</name>
  <coder>Anton Retro</coder>
  <version>0.1</version>
  <short_description>Hotel Mario (CD-i) port</short_description>
  <long_description>A native port of Hotel Mario (Philips CD-i, 1994) made from the game's own code. Bring your own
disc: copy its extracted files to sd:/apps/hotelmario/game/. Wii Remote sideways: d-pad moves, 1 and 2 are the CD-i
buttons, HOME quits. GameCube controller: A and B, START + Z quits.</long_description>
</app>
"""


def compile_one(args):
    src, out, flags = args
    obj = os.path.join(out, "obj", os.path.basename(src)[:-2] + ".o")
    deps = [src] + glob.glob(os.path.join(ROOT, "port", "*.h"))
    if os.path.exists(obj) and os.path.getmtime(obj) >= max(os.path.getmtime(d) for d in deps):
        return obj, None
    r = subprocess.run([GCC, "-c"] + flags + [src, "-o", obj], capture_output=True, text=True)
    return obj, (r.stderr if r.returncode else None)


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "wii"
    if target not in MACHINE:
        sys.exit("usage: build_ogc.py [wii|gc]")
    if not os.path.exists(GCC):
        sys.exit("devkitPPC missing: install devkitPro with the Wii / GameCube packages")
    recomp = sorted(glob.glob(os.path.join(ROOT, "build", "recomp", "*.c")))
    if not recomp:
        sys.exit("build/recomp is empty: run tools/build_port.py first")
    out = os.path.join(ROOT, "build", target)
    os.makedirs(os.path.join(out, "obj"), exist_ok=True)
    flags = MACHINE[target] + ["-mcpu=750", "-meabi", "-mhard-float", "-O2", "-w", "-DGEKKO",
                               "-I" + os.path.join(ROOT, "port"), "-I" + MUSASHI, '-DMUSASHI_CNF="musashi_conf.h"',
                               "-I" + os.path.join(OGC, "include")]
    srcs = recomp + [s for s in sorted(glob.glob(os.path.join(ROOT, "port", "*.c"))) if os.path.basename(s) not in PC_ONLY]
    srcs += sorted(glob.glob(os.path.join(ROOT, "port", "ogc", "*.c")))
    srcs += [os.path.join(MUSASHI, f) for f in ("m68kcpu.c", "m68kops.c", "m68kdasm.c")]
    srcs += [os.path.join(MUSASHI, "softfloat", "softfloat.c")]
    objs, failed = [], False
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for obj, err in pool.map(compile_one, [(s, out, flags) for s in srcs]):
            objs.append(obj)
            if err:
                failed = True
                print(err[:4000])
    if failed:
        sys.exit("compile failed")
    elf = os.path.join(out, "HotelMario.elf")
    lib = os.path.join(OGC, "lib", "wii" if target == "wii" else "cube")
    subprocess.run([GCC] + MACHINE[target][:1] + ["-mcpu=750", "-meabi", "-mhard-float"] + objs +
                   ["-L" + lib] + LIBS[target] + ["-o", elf], check=True)
    if target == "wii":
        app = os.path.join(out, "apps", "hotelmario")
        os.makedirs(app, exist_ok=True)
        dol = os.path.join(app, "boot.dol")
        with open(os.path.join(app, "meta.xml"), "w", encoding="utf-8") as f:
            f.write(META)
    else:
        dol = os.path.join(out, "HotelMario.dol")
    subprocess.run([ELF2DOL, elf, dol], check=True)
    print("built", dol, os.path.getsize(dol) // 1024, "KB")


if __name__ == "__main__":
    main()
