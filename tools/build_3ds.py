"""Build the 3DS port: the same recompiled game and runtime as the PC port, with port/3ds/ in place of the
SDL and Windows files. Needs devkitPro (devkitARM + libctru) and the PC build's recompiled C (build/recomp,
made by tools/build_port.py or tools/recomp.py).

Usage: python tools/build_3ds.py   -> build/3ds/HotelMario.3dsx
Copy it to sdmc:/3ds/HotelMario/ and your extracted disc files (build/eu/fs) to sdmc:/3ds/HotelMario/game/.
"""
import concurrent.futures
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DKP = os.environ.get("DEVKITPRO_WIN", r"C:\devkitPro")
GCC = os.path.join(DKP, "devkitARM", "bin", "arm-none-eabi-gcc.exe")
CTRU = os.path.join(DKP, "libctru")
TOOLS = os.path.join(DKP, "tools", "bin")
OUT = os.path.join(ROOT, "build", "3ds")
MUSASHI = os.path.join(ROOT, "tools", "ext", "Musashi")
PC_ONLY = {"window.c", "audio.c", "gamefiles.c"}   # replaced by port/3ds/

ARCH = ["-march=armv6k", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft"]
FLAGS = ARCH + ["-O2", "-w", "-mword-relocations", "-ffunction-sections", "-D__3DS__",
                "-I" + os.path.join(ROOT, "port"), "-I" + MUSASHI, '-DMUSASHI_CNF="musashi_conf.h"',
                "-I" + os.path.join(CTRU, "include")]


def compile_one(src):
    obj = os.path.join(OUT, "obj", os.path.basename(src)[:-2] + ".o")
    deps = [src] + glob.glob(os.path.join(ROOT, "port", "*.h"))
    if os.path.exists(obj) and os.path.getmtime(obj) >= max(os.path.getmtime(d) for d in deps):
        return obj, None
    r = subprocess.run([GCC, "-c"] + FLAGS + [src, "-o", obj], capture_output=True, text=True)
    return obj, (r.stderr if r.returncode else None)


def main():
    if not os.path.exists(GCC):
        sys.exit("devkitARM missing: install devkitPro with the 3DS packages")
    recomp = sorted(glob.glob(os.path.join(ROOT, "build", "recomp", "*.c")))
    if not recomp:
        sys.exit("build/recomp is empty: run tools/build_port.py (or tools/recomp.py) first")
    os.makedirs(os.path.join(OUT, "obj"), exist_ok=True)
    srcs = recomp + [s for s in sorted(glob.glob(os.path.join(ROOT, "port", "*.c")))
                     if os.path.basename(s) not in PC_ONLY]
    srcs += sorted(glob.glob(os.path.join(ROOT, "port", "3ds", "*.c")))
    srcs += [os.path.join(MUSASHI, f) for f in ("m68kcpu.c", "m68kops.c", "m68kdasm.c")]
    srcs += [os.path.join(MUSASHI, "softfloat", "softfloat.c")]
    objs, failed = [], False
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for obj, err in pool.map(compile_one, srcs):
            objs.append(obj)
            if err:
                failed = True
                print(err[:4000])
    if failed:
        sys.exit("compile failed")
    elf = os.path.join(OUT, "HotelMario.elf")
    subprocess.run([GCC, "-specs=3dsx.specs"] + ARCH + objs + ["-L" + os.path.join(CTRU, "lib"),
                    "-lctru", "-lm", "-o", elf], check=True)
    smdh = os.path.join(OUT, "HotelMario.smdh")
    subprocess.run([os.path.join(TOOLS, "smdhtool.exe"), "--create", "Hotel Mario", "CD-i port (bring your own disc)",
                    "Anton Retro", os.path.join(CTRU, "default_icon.png"), smdh], check=True)
    out = os.path.join(OUT, "HotelMario.3dsx")
    subprocess.run([os.path.join(TOOLS, "3dsxtool.exe"), elf, out, "--smdh=" + smdh], check=True)
    print("built", out, os.path.getsize(out) // 1024, "KB")
    if "--cia" in sys.argv:
        build_cia(elf)


def build_cia(elf):
    """--cia: an installable HotelMario.cia (home menu icon and banner). The pictures come from your own disc,
    so they live in build/cia, not in git: banner.png (256x128), icon.png (48x48) and banner.wav (under 3 s).
    The ones in use were cut from frames of the port: the title screen's logo, sky and scenery, with Mario's
    walking and jumping sprites from a stage."""
    art = os.path.join(ROOT, "build", "cia")
    need = [os.path.join(art, f) for f in ("banner.png", "icon.png", "banner.wav")]
    missing = [f for f in need if not os.path.exists(f)]
    if missing:
        sys.exit("for --cia, make these first: " + ", ".join(missing))
    bt = os.path.join(DKP, "tools", "ctr_extra", "bt", "windows-x86_64", "bannertool.exe")
    makerom = os.path.join(DKP, "tools", "ctr_extra", "makerom.exe")
    bnr, icn, cia = (os.path.join(art, f) for f in ("banner.bnr", "icon.icn", "HotelMario.cia"))
    subprocess.run([bt, "makebanner", "-i", need[0], "-a", need[2], "-o", bnr], check=True)
    subprocess.run([bt, "makesmdh", "-s", "Hotel Mario", "-l", "Hotel Mario (CD-i) - bring your own disc",
                    "-p", "Anton Retro", "-i", need[1], "-o", icn], check=True)
    subprocess.run([makerom, "-f", "cia", "-o", cia, "-elf", elf, "-rsf", os.path.join(ROOT, "port", "3ds", "app.rsf"),
                    "-icon", icn, "-banner", bnr, "-exefslogo", "-target", "t"], check=True)
    print("built", cia, os.path.getsize(cia) // 1024, "KB")


if __name__ == "__main__":
    main()
