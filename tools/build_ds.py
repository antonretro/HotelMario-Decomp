"""Build the DS port: the same recompiled game and runtime as the PC port, with port/ds/ in place of the SDL and
Windows files. Needs BlocksDS (Wonderful toolchain) and the PC build's recompiled C (build/recomp, made by
tools/build_port.py or tools/recomp.py).

Usage: python tools/build_ds.py          -> build/ds/HotelMario.nds
Copy it anywhere on the SD card, and your disc image (the .bin) to /hotelmario/ on the same card.
       python tools/build_ds.py --full   -> also build/ds/HotelMario_full.nds, with your disc image inside it (367 MB),
so it's the only file you need. It holds your whole game: keep it to yourself.
"""
import concurrent.futures
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WONDERFUL = os.environ.get("WONDERFUL_WIN", "C:/msys64/opt/wonderful")
BLOCKSDS = os.environ.get("BLOCKSDS_WIN", WONDERFUL + "/thirdparty/blocksds/core")
GCC = WONDERFUL + "/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe"
SIZE = WONDERFUL + "/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-size.exe"
NDSTOOL = BLOCKSDS + "/tools/ndstool/ndstool.exe"
SPECS = BLOCKSDS + "/sys/crts/ds_arm9.specs"
OUT = os.path.join(ROOT, "build", "ds")
PORT = os.path.join(ROOT, "port")
DS = os.path.join(PORT, "ds")
MUSASHI = os.path.join(ROOT, "tools", "ext", "Musashi")
PC_ONLY = {"window.c", "audio.c", "gamefiles.c", "display.c"}   # replaced by port/ds/ (display.c is included there)

ARCH = ["-mcpu=arm946e-s+nofp"]
COMMON = ARCH + ["-w", "-ffunction-sections", "-fdata-sections", "-D__NDS__", "-D__BLOCKSDS__", "-DARM9", "-specs=" + SPECS,
                 "-I" + PORT, "-I" + DS, "-I" + MUSASHI, '-DMUSASHI_CNF="musashi_conf.h"',
                 "-I" + BLOCKSDS + "/libs/libnds/include"]
# Most of the game runs seldom and Thumb keeps it small; the interpreter is the hot part, in full ARM code.
GAME = COMMON + ["-mthumb", "-Os", "-include", os.path.join(DS, "ds_recomp.h")]
RUNTIME = COMMON + ["-mthumb", "-O2", "-include", os.path.join(DS, "ds_prefix.h")]
INTERP = COMMON + ["-marm", "-O2", "-include", os.path.join(DS, "ds_prefix.h")]


def compile_one(job):
    src, flags = job
    obj = os.path.join(OUT, "obj", os.path.basename(src)[:-2] + ".o")
    deps = [src] + glob.glob(os.path.join(PORT, "*.h")) + glob.glob(os.path.join(DS, "*.h"))
    if os.path.basename(src) == "display_ds.c":
        deps.append(os.path.join(PORT, "display.c"))
    if os.path.exists(obj) and os.path.getmtime(obj) >= max(os.path.getmtime(d) for d in deps):
        return obj, None
    r = subprocess.run([GCC, "-c"] + flags + [src, "-o", obj], capture_output=True, text=True)
    return obj, (r.stderr if r.returncode else None)


def main():
    if not os.path.exists(GCC):
        sys.exit("BlocksDS missing: install the Wonderful toolchain with blocksds-toolchain")
    recomp = sorted(glob.glob(os.path.join(ROOT, "build", "recomp", "*.c")))
    if not recomp:
        sys.exit("build/recomp is empty: run tools/build_port.py (or tools/recomp.py) first")
    if not os.path.exists(os.path.join(MUSASHI, "m68kops.c")):
        sys.exit("Musashi missing or not generated: run tools/build_port.py first")
    os.makedirs(os.path.join(OUT, "obj"), exist_ok=True)
    jobs = [(s, GAME) for s in recomp]
    jobs += [(s, RUNTIME) for s in sorted(glob.glob(os.path.join(PORT, "*.c"))) if os.path.basename(s) not in PC_ONLY]
    jobs += [(s, RUNTIME) for s in sorted(glob.glob(os.path.join(DS, "*.c")))]
    jobs += [(os.path.join(MUSASHI, f), INTERP) for f in ("m68kcpu.c", "m68kops.c")]
    jobs += [(os.path.join(MUSASHI, "softfloat", "softfloat.c"), INTERP)]
    objs, failed = [], False
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for obj, err in pool.map(compile_one, jobs):
            objs.append(obj)
            if err:
                failed = True
                print(err[:4000])
    if failed:
        sys.exit("compile failed")
    elf = os.path.join(OUT, "HotelMario.elf")
    env = dict(os.environ, BLOCKSDS=BLOCKSDS)
    subprocess.run([GCC] + ARCH + ["-mthumb", "-specs=" + SPECS, "-L" + BLOCKSDS + "/libs/libnds/lib"] + objs +
                   ["-Wl,--start-group", "-lnds9", "-lc", "-Wl,--end-group", "-Wl,-Map," + elf[:-4] + ".map", "-o", elf],
                   check=True, env=env)
    subprocess.run([SIZE, elf], check=True)
    out = os.path.join(OUT, "HotelMario.nds")
    subprocess.run([NDSTOOL, "-c", out, "-7", BLOCKSDS + "/sys/arm7/main_core/arm7_minimal.elf", "-9", elf,
                    "-b", BLOCKSDS + "/sys/icon.gif", "Hotel Mario;CD-i port (bring your own disc);Anton Retro"], check=True)
    print("built", out, os.path.getsize(out) // 1024, "KB")
    if "--full" in sys.argv:
        build_full(elf)


def build_full(elf):
    """--full: the same program, with your disc image in the ROM's own file system (NitroFS)."""
    images = sorted(glob.glob(os.path.join(ROOT, "disc", "orig", "*", "*.bin")))
    if not images:
        sys.exit("for --full, put your disc image in disc/orig/<any folder>/")
    nitro = os.path.join(OUT, "nitro")
    inside = os.path.join(nitro, "hotel.bin")
    os.makedirs(nitro, exist_ok=True)
    if not os.path.exists(inside):
        os.link(images[0], inside)
    out = os.path.join(OUT, "HotelMario_full.nds")
    subprocess.run([NDSTOOL, "-c", out, "-7", BLOCKSDS + "/sys/arm7/main_core/arm7_minimal.elf", "-9", elf,
                    "-b", BLOCKSDS + "/sys/icon.gif", "Hotel Mario;CD-i port (bring your own disc);Anton Retro",
                    "-d", nitro], check=True)
    print("built", out, os.path.getsize(out) // (1024 * 1024), "MB")


if __name__ == "__main__":
    main()
