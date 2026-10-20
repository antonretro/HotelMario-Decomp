# Hotel Mario decomp

A decompilation of **Hotel Mario** (Fantasy Factory / Philips, CD-i, 1994), plus a BIOS-free PC port that
runs the game from your own disc image. **The port is playable from the title screen to the ending credits.**

## Bring your own disc

This repository contains **no game data**: no disc image, no extracted files, no game code. You supply your
own copy of the game, either the **USA** or the **European** release, and the tools extract everything
they need from it into `build/` and `asm/`. Both folders are git-ignored and must never be committed.

| Disc image | SHA1 of the `.bin` | Status |
|---|---|---|
| Hotel Mario (Europe) | `8d671f7bc729ed8d6a07d1d40ba48cbc9530bd30` | Supported: matching build and PC port |
| Hotel Mario (USA) | - | Planned: the port needs the USA program translated as well |
| Hotel Mario (USA) (Beta) (1993-11-23) | `183b2efd1f5b926757a8fb514cc861fdf7a244c5` | Recognised, used for comparison |

## What's here

There are two separate goals. The port is how you play the game today; the decompilation is the long-term
job of rebuilding readable source code. The port doesn't depend on it.

- **PC port (a static recompilation).** `tools/recomp.py` translates the game's 68000 code into C ahead of
  time, so the original game logic runs natively. `port/` is the runtime around it:
  - OS-9 system calls and CD-RTOS disc streaming, in place of the CD-i operating system (no BIOS needed)
  - the CD-i video chip (both image planes, palettes, DYUV, run-length animation, transparency, mixing,
    fades)
  - CD-i ADPCM audio and the sound maps used for the stage music and effects
  - the CD-i's non-volatile memory, for saves
  - an SDL2 window with keyboard and gamepad input, and a settings menu

  Code that the game streams from the disc runs on the Musashi 68000 interpreter.
- **Matching decompilation.** The original program `cdi_hotel` is disassembled and reassembles
  byte-for-byte (`tools/build.py`). Functions are being rewritten as K&R C in `src/` (Microware OS-9/68000
  C 3.2, the compiler the game was built with). About 14% of the program is in C so far. The C is
  equivalent to the original but doesn't yet compile to identical bytes, because that needs the original
  Microware compiler.

### Port status

| | |
|---|---|
| Start-up | Philips logo, Fantasy Factory logo, the intro cartoon with speech, title screen and music |
| Gameplay | All seven hotels: map, cutscenes, stage cards and stages, with music and sound effects |
| Saves | Save, RESTORE and high scores, stored in a `saves/` folder |
| Ending | Bowser's palace, the ending cartoon and the full scrolling credits |
| Smoothness | Even frame pacing (48–50 pictures a second in play), no flicker or flashes between scenes |

Every hotel and the ending have been checked with scripted test runs; a complete play-through by hand
hasn't been done yet. Next: an optional smooth-enemies setting (with a Classic setting for the original
look), USA disc support, then 3DS and Wii builds.

## Building

Requirements:
- Python 3
- MinGW-w64 gcc (`C:\mingw64\bin\gcc.exe`, or set `CC`)
- [vasm](http://sun.hasenbraten.de/vasm/) for the matching build: built for 68k with mot syntax, as `tools/ext/vasm/vasmm68k_mot`
- in `tools/ext/` for the port:
  - [Musashi](https://github.com/kstenerud/Musashi): `git clone --depth 1 https://github.com/kstenerud/Musashi.git tools/ext/Musashi`
  - [SDL 2.32.10](https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10) (MinGW development package)

```bash
python -I tools/setup.py "path/to/Hotel Mario (Europe).bin"
python -I tools/build.py
python -I tools/build_port.py
```

1. `setup.py` checks your image, extracts it to `build/eu/fs` and generates `asm/`.
2. `build.py` reassembles `cdi_hotel` and checks it matches the original.
3. `build_port.py` builds `build/port/hotel.exe`.

Run the exe from the repository folder, or pass the path of the extracted files:
`hotel.exe path\to\build\eu\fs`. Saves and `hotel_settings.ini` are written to the current folder.

**Controls:**
- arrows / left stick / d-pad move
- Z, Space or A is button 1
- X, Enter or B is button 2
- F1 (or the pad's Back button) opens the settings
- F11 toggles fullscreen
- F12 saves a frame for debugging
- Esc quits

**Saving:** on a stage card ("Press Button One To Begin"), hold down and press button 2. RESTORE under
PLAYER 1 on the title menu lists your saves.

**Settings (F1):** extras the CD-i never had, each one can be turned off: fullscreen, window size,
picture crop (TV, edges or none), smooth scaling, redraw at the monitor's rate, volume, skip the Philips
logo, game speed, and smooth frame pacing (on by default).

**Tests:**
- `python -I tools/startup_test.py` runs the boot headless and checks every milestone (logos, intro, sound).
- `python -I tools/hotel_sweep.py` visits each hotel through a save and RESTORE, and leaves a contact sheet
  of frames per hotel in `build/port/`.

## Notes

- [NOTES.md](NOTES.md): file formats, compiler identification, and how each port problem was solved.
- [TRIVIA.md](TRIVIA.md): hidden and cut content found along the way.
- [docs/TECHNICAL.md](docs/TECHNICAL.md): how Hotel Mario works on the CD-i, and how the port was made.

## Credits

- **Musashi** 68000 core, (c) Karl Stenerud, MIT licence. Not included; fetched into `tools/ext`.
- **SDL2**, zlib licence. Not included; fetched into `tools/ext`.
- **vasm**, by Volker Barthelmann and Frank Wille.
- Microware's *CD-I System Libraries Technical Reference Manual*, for the CD-RTOS structures.
- [cdirecomp](https://github.com/mstan/cdirecomp) by mstan is a separate CD-i static recompiler that runs
  Hotel Mario using a real CD-i system ROM. It's prior art, credited here; none of its code is used.

Hotel Mario is © Nintendo / Philips. This project is not affiliated with or endorsed by Nintendo, Philips
or Fantasy Factory. You need your own legally obtained copy of the game.
