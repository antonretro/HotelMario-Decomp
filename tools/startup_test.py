"""Start-up regression test for the PC port: run it headless a few times and check each milestone.

Usage: python tools/startup_test.py [exe] [runs] [seconds]      (defaults build/port/hotel_dev2.exe 3 90)

Milestones, in order:
  logo_play   the Philips logo program streams bumper.rtf to its last sector (624)
  logo_exit   the logo program exits and control returns to the game
  ff_logo     the game streams the Fantasy Factory logo (sector 12 of the intro data)
  intro_anim  the game reaches the intro animation (sector 80 of intro.rtf)
  intro_more  the game reaches the next intro part (sector 157)
  intro_end   the intro cartoon streams to its end (sector 3782); the title screen follows
  logo_sound  audio sectors reached the speakers during the Philips logo (its jingle)
  intro_sound and during the intro (speech)
and that nothing crashed ("stopped:" in the log).
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECKS = [
    ("logo_play", r"end of play[^\n]* at sector 624"),
    ("logo_exit", r"child cdi_bumper finished"),
    ("ff_logo", r"play from sector 12:"),
    ("intro_anim", r"play from sector 80:"),
    ("intro_more", r"play from sector 157:"),
    ("intro_end", r"end of play[^\n]* at sector 3782"),
    ("logo_sound", r"at sector 624 \([1-9]\d* audio sectors"),       # the Philips jingle reached the speakers
    ("intro_sound", r"at sector 3782 \([1-9]\d{2,} audio sectors"),  # and the intro speech after it
]


def run_once(exe, seconds, n):
    log = os.path.join(ROOT, "build", "port", f"startup{n}.log")
    with open(log, "w") as err:
        try:
            subprocess.run([exe, "--trace", "--headless"], cwd=ROOT, stderr=err, stdout=subprocess.DEVNULL,
                           timeout=seconds)
        except subprocess.TimeoutExpired:
            pass
    text = open(log, errors="replace").read()
    results = {name: bool(re.search(pat, text)) for name, pat in CHECKS}
    results["no_crash"] = "stopped:" not in text
    events = re.findall(r"\[cd-i\] (play from sector \d+|end of play[^\n]*? at sector \d+|play aborted at sector \d+)|"
                        r"(child \S+ finished)", text)
    seq = [re.sub(r"end of play \((records done|end of file)\)", lambda m: "end" + ("R" if "records" in m.group(1) else "F"),
                  a) if a else b for a, b in events]
    return results, seq


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "port", "hotel_dev2.exe")
    runs = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 90
    all_ok = True
    for n in range(1, runs + 1):
        results, seq = run_once(exe, seconds, n)
        ok = all(results.values())
        all_ok &= ok
        marks = " ".join(f"{k}={'OK' if v else 'FAIL'}" for k, v in results.items())
        print(f"run {n}: {'PASS' if ok else 'FAIL'}  {marks}")
        print("   " + " -> ".join(s.replace("play from sector ", "play ").replace("end of play at sector ", "end@")
                                  .replace("play aborted at sector ", "abort@") for s in seq[:14]))
    print("ALL PASS" if all_ok else "SOME FAILED")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
