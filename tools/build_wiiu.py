"""Build the Wii U port: compiles the statically recompiled CD-i 68000 code and
runtime for the Wii U using devkitPPC + wut + SDL2, then packages an Aroma .wuhb.
"""
import concurrent.futures
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DKP = os.environ.get("DEVKITPRO_WIN", r"C:\devkitPro")
os.environ["DEVKITPRO"] = DKP.replace("\\", "/")

GCC = os.path.join(DKP, "devkitPPC", "bin", "powerpc-eabi-gcc.exe")
WUT = os.path.join(DKP, "wut")
PORTLIBS = os.path.join(DKP, "portlibs", "wiiu")
TOOLS = os.path.join(DKP, "tools", "bin")
OUT = os.path.join(ROOT, "build", "wiiu")
MUSASHI = os.path.join(ROOT, "tools", "ext", "Musashi")
CONTENT = os.path.join(ROOT, "build", "eu", "fs")

PC_ONLY = {"gamefiles.c"}  # replaced by port/wiiu/files_wiiu.c

ARCH = ["-DESPRESSO", "-mcpu=750", "-meabi", "-mhard-float", "-D__WIIU__", "-D__WUT__"]
FLAGS = ARCH + [
    "-O2", "-w", "-ffunction-sections", "-fdata-sections",
    "-I" + os.path.join(ROOT, "port"),
    "-I" + MUSASHI,
    "-DMUSASHI_CNF=<musashi_conf.h>",
    "-I" + os.path.join(WUT, "include"),
    "-I" + os.path.join(PORTLIBS, "include"),
    "-I" + os.path.join(PORTLIBS, "include", "SDL2")
]


def compile_one(src):
    obj = os.path.join(OUT, "obj", os.path.basename(src)[:-2] + ".o")
    deps = [src] + glob.glob(os.path.join(ROOT, "port", "*.h"))
    if os.path.exists(obj) and os.path.getmtime(obj) >= max(os.path.getmtime(d) for d in deps):
        return obj, None
    r = subprocess.run([GCC, "-c"] + FLAGS + [src, "-o", obj], capture_output=True, text=True)
    return obj, (r.stderr if r.returncode else None)


def main():
    if not os.path.exists(GCC):
        sys.exit("devkitPPC missing: check devkitPro installation")
    recomp = sorted(glob.glob(os.path.join(ROOT, "build", "recomp", "*.c")))
    if not recomp:
        sys.exit("build/recomp is empty: run tools/recomp.py first")
    os.makedirs(os.path.join(OUT, "obj"), exist_ok=True)

    srcs = recomp + [s for s in sorted(glob.glob(os.path.join(ROOT, "port", "*.c")))
                     if os.path.basename(s) not in PC_ONLY]
    srcs += sorted(glob.glob(os.path.join(ROOT, "port", "wiiu", "*.c")))
    srcs += [os.path.join(MUSASHI, f) for f in ("m68kcpu.c", "m68kops.c", "m68kdasm.c")]
    srcs += [os.path.join(MUSASHI, "softfloat", "softfloat.c")]

    print(f"Compiling {len(srcs)} sources for Wii U (PowerPC)...")
    objs, failed = [], False
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for obj, err in pool.map(compile_one, srcs):
            objs.append(obj)
            if err:
                failed = True
                print("Error in", obj, ":\n", err[:4000])
    if failed:
        sys.exit("compile failed")

    elf = os.path.join(OUT, "HotelMario.elf")
    rpx = os.path.join(OUT, "HotelMario.rpx")
    wuhb = os.path.join(OUT, "HotelMario.wuhb")

    print("Linking HotelMario.elf...")
    link_cmd = [
        GCC,
        f"-specs={os.path.join(WUT, 'share', 'wut.specs')}",
        "-Wl,--gc-sections"
    ] + ARCH + objs + [
        "-L" + os.path.join(PORTLIBS, "lib"),
        "-L" + os.path.join(WUT, "lib"),
        "-lSDL2", "-lwut", "-lm",
        "-o", elf
    ]
    subprocess.run(link_cmd, check=True)
    print("Converting to RPX...")
    subprocess.run([os.path.join(TOOLS, "elf2rpl.exe"), elf, rpx], check=True)

    print("Packaging WUHB...")
    wuhb_cmd = [
        os.path.join(TOOLS, "wuhbtool.exe"),
        rpx, wuhb,
        "--name", "Hotel Mario",
        "--short-name", "Hotel Mario",
        "--author", "Philips / Fantasy Factory"
    ]
    if os.path.isdir(CONTENT):
        wuhb_cmd.append(f"--content={CONTENT}")
    subprocess.run(wuhb_cmd, check=True)

    print(f"\nBuilt successfully: {wuhb} ({os.path.getsize(wuhb) // 1024} KB)")


if __name__ == "__main__":
    main()
