"""Build the PC port: recompile cdi_hotel from your disc, then compile it with the runtime.

Usage: python tools/build_port.py            -> build/port/hotel.exe
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GCC = os.environ.get("CC", r"C:\mingw64\bin\gcc.exe")
OUT = os.path.join(ROOT, "build", "port")


def main():
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "recomp.py")], check=True, cwd=ROOT)
    os.makedirs(OUT, exist_ok=True)
    srcs = sorted(glob.glob(os.path.join(ROOT, "build", "recomp", "*.c"))) + \
        sorted(glob.glob(os.path.join(ROOT, "port", "*.c")))
    objs = []
    for s in srcs:
        o = os.path.join(OUT, os.path.basename(s)[:-2] + ".o")
        if not os.path.exists(o) or os.path.getmtime(o) < max(os.path.getmtime(s), os.path.getmtime(os.path.join(ROOT, "port", "m68k.h"))):
            subprocess.run([GCC, "-c", "-O1", "-w", "-I" + os.path.join(ROOT, "port"), s, "-o", o], check=True)
        objs.append(o)
    exe = os.path.join(OUT, "hotel.exe")
    subprocess.run([GCC] + objs + ["-o", exe], check=True)
    print("built", exe)


if __name__ == "__main__":
    main()
