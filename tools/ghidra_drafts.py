"""Run Ghidra headless on cdi_hotel and export a C draft per function into build/eu/ghidra/.

Usage: python tools/ghidra_drafts.py [function names...]
Needs Ghidra 10.4 in tools/ext/ghidra_10.4_PUBLIC (Java 17+). The Ghidra project is kept in
build/ghidra so later runs only re-export.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GHIDRA = os.path.join(ROOT, "tools", "ext", "ghidra_10.4_PUBLIC", "support", "analyzeHeadless.bat")
SCRIPTS = os.path.join(ROOT, "tools", "ghidra")
FS = os.path.join(ROOT, "build", "eu", "fs")
SYMS = os.path.join(ROOT, "build", "eu", "syms.txt")
PROJ = os.path.join(ROOT, "build", "ghidra")
OUT = os.path.join(ROOT, "build", "eu", "ghidra")


def main():
    names = sys.argv[1:]
    if not os.path.exists(SYMS):
        with open(SYMS, "w") as f:
            subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "stb.py"),
                            os.path.join(FS, "cdi_hotel.stb")], stdout=f, check=True)
    os.makedirs(PROJ, exist_ok=True)
    exists = os.path.exists(os.path.join(PROJ, "hotel.gpr"))
    cmd = [GHIDRA, PROJ, "hotel", "-scriptPath", SCRIPTS]
    if exists:
        cmd += ["-process", "cdi_hotel", "-noanalysis"]
    else:
        cmd += ["-import", os.path.join(FS, "cdi_hotel"), "-loader", "BinaryLoader",
                "-processor", "68000:BE:32:default",
                "-preScript", "HotelSetup.py", SYMS, os.path.join(FS, "cdi_hotel_data")]
    cmd += ["-postScript", "ApplyCC.py", os.path.join(ROOT, "asm", "funcs")]
    cmd += ["-postScript", "ExportC.py", OUT] + names
    r = subprocess.run(cmd, capture_output=True, text=True)
    for line in (r.stdout + r.stderr).splitlines():
        if any(k in line for k in ("HotelSetup:", "ApplyCC:", "ExportC:", "ERROR", "Exception", "Traceback")):
            print(line.strip())
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
