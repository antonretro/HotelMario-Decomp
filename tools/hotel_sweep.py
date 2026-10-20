"""Visit each hotel through RESTORE: write a save for hotel N stage 1, restore it, start the stage.

Usage: python tools/hotel_sweep.py [exe] [hotels, e.g. 1-7] [seconds per run]
Each run leaves build/port/sweep_hN.png (frames from the menu to the stage) and sweep_hN.log.
Your own saves/hotel_mario is put back afterwards.
"""
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAVE = os.path.join(ROOT, "saves", "hotel_mario")
FRAMES = os.path.join(ROOT, "build", "port", "frames")
# title: 1 PLAYER, PLAYER 1, down to RESTORE, choose it, pick the save; on the map step up to the hotel
# (the cursor starts on hotel 1) and choose it; then keep pressing button 1 through the cutscene and card
def input_for(hotel):
    steps = [f"{48500 + 1500 * i}:up" for i in range(hotel - 1)]   # up/left: next hotel, down/right: previous
    t = 48500 + 1500 * (hotel - 1) + 1000
    return ",".join(["30000:b1", "36000:b1", "38500:down", "40000:b1", "45000:b1"] + steps + [f"{t}:b1"] +
                    [f"{s}:b1" for s in range(t + 6000, t + 50000, 3000)])


def save_for(hotel):
    rec = bytearray(62)
    rec[0x24:0x27] = b"SWP"
    rec[0x34:0x36] = ((hotel << 8) + 1).to_bytes(2, "big")    # (hotel << 8) + stage + 1, stage 0
    return bytes(rec)


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "port", "hotel.exe")
    lo, hi = (int(x) for x in (sys.argv[2] if len(sys.argv) > 2 else "1-7").split("-"))
    secs = int(sys.argv[3]) if len(sys.argv) > 3 else 95
    os.makedirs(os.path.dirname(SAVE), exist_ok=True)
    backup = open(SAVE, "rb").read() if os.path.exists(SAVE) else None
    try:
        for h in range(lo, hi + 1):
            open(SAVE, "wb").write(save_for(h))
            shutil.rmtree(FRAMES, ignore_errors=True)
            os.makedirs(FRAMES)
            log = os.path.join(ROOT, "build", "port", f"sweep_h{h}.log")
            with open(log, "w") as err:
                p = subprocess.Popen([exe, "--headless", "--dump-every", "1000", "--input", input_for(h)], cwd=ROOT,
                                     stderr=err, stdout=subprocess.DEVNULL)
                time.sleep(secs)
                p.kill()
            subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "contact_sheet.py"),
                            os.path.join(ROOT, "build", "port", f"sweep_h{h}.png"), "10", "", "40000",
                            str(secs * 1000), "1000"], cwd=ROOT)
    finally:
        if backup is None:
            os.remove(SAVE)
        else:
            open(SAVE, "wb").write(backup)


if __name__ == "__main__":
    main()
