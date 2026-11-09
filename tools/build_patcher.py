"""Build the release setup program, build/release/Hotel Mario setup.exe (tools/patcher/patcher.c).

Usage: python -I tools/build_patcher.py

Needs MinGW-w64 gcc (C:\\mingw64\\bin\\gcc.exe, or set CC). No other libraries.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CC = os.environ.get("CC", r"C:\mingw64\bin\gcc.exe")


def main():
    out_dir = os.path.join(ROOT, "build", "release")
    os.makedirs(out_dir, exist_ok=True)
    exe = os.path.join(out_dir, "Hotel Mario setup.exe")
    cmd = [CC, "-O2", "-Wall", "-Wno-unused-function", "-mwindows", "-static",
           os.path.join(ROOT, "tools", "patcher", "patcher.c"), "-o", exe,
           "-lcomctl32", "-lcomdlg32", "-lshell32", "-lole32"]
    r = subprocess.run(cmd)
    if r.returncode:
        sys.exit("build failed")
    print("built", exe)


if __name__ == "__main__":
    main()
