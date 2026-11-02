# Hotel Mario on the Nintendo DS

**Verdict: a native DS port looks realistic, on a plain 4 MB DS as well as a DSi.** The earlier "not possible"
came from reading the port's 16 MB flat memory image as what the game needs. It needs under 1 MB.

`python tools/build_ds.py` builds `build/ds/HotelMario.nds`, and `--full` a one-file ROM with your disc inside. The game
runs in melonDS. It has not been tried on a real DS yet. Everything below marked *measured* was measured on the PC, and
everything marked *estimate* still needs a real speed reading.

## Numbers

### Memory (measured, PC port, from boot into Hotel 1 Stage 1)

| | |
|---|---|
| All memory the game ever asks for | 949,666 bytes (includes the Philips logo's) |
| Largest single request | 431,392 bytes |
| Memory actually written | 860 KB at most |
| The real CD-i | about 1 MB in total, so the game has to fit |

| In a 4 MB DS (3.5 MB usable) | |
|---|---|
| CD-i memory | 1,280 KB, was 16 MB |
| All code and constants, Musashi's 270 KB included | 1,129 KB |
| Musashi tables | 576 KB |
| Stack | 128 KB |
| Everything else | about 350 KB |
| **Free** | **about 124 KB** |

### Recompiled code size (measured, 43,322 68K instructions, for the DS's ARM946E-S, with the `--watch` checks still in)

| Build | Size | |
|---|---|---|
| ARM -O2 | 1,298 KB | 30 bytes per 68K instruction |
| Thumb -O2 | 1,067 KB | |
| Thumb -Os | 753 KB | what the build uses |

### CPU (measured work, estimated cost)

The game mostly waits. In a stage the PC ran 500-640 million 68K instructions a second, nearly all in `the_game`,
`exit_check` and an empty `ds_dispatch_execute`, which only ask whether the next frame is due. Only about
**55,000 a second** are real work, plus about **28,000** on Musashi.

| Stage, on a 67 MHz DS | Cycles/s | Share |
|---|---|---|
| Recompiled game: 55k x 40 | 2.2 M | 3% |
| Musashi: 28k x 200 | 5.6 M | 8% |
| 30 pictures a second, stage zoom off: about 31,000 words each | 11.3 M | 17% |
| (zoom on: about 24,000 words each) | (8.8 M) | (13%) |
| Audio: 18.75 sectors x 4032 samples x 25 | 1.9 M | 3% |
| Disc stream, 176 KB/s through memory | 1 M | 2% |
| SD card reads | depends on the card | 10-20%? |
| **Total** | | **about 40-50%** |

The cycle costs per item are estimates until the bottom screen's speed line reports real ones.

## How it works

- **Video.** The CD-i's two planes map almost directly onto the DS's 2D engine (`port/ds/display_ds.c`). Every stage is
  CLUT7 on both planes, which is plain copies into two 8-bit bitmap layers. The hardware does the mixing, the colour key,
  the plane order and the 1.5 horizontal step. Vertically, one line in five is left out, turning lines 2-241 into 192.
  Together those give the TV's 4:3 shape. DYUV stills are decoded to a 16-bit layer only when they change and show.
- **Stages.** A stage is when `mario()` is running, which comes from the game's own code. The score bar is laid out again
  to fit at 1:1 on a black bar. X picks the picture under it: the whole stage (the default), or 1:1 with a camera that
  eases after Mario, which also copies 30% less.
- **Text.** On other pictures the game draws itself, plane A (the text) stays 1:1 and plane B stays fitted, so a menu keeps
  its whole background. A line too long for 256 pixels loses the empty column of each letter. Cutscenes, logos and DYUV
  stills always fit.
- **Timing.** Drawing starts at the start of the blank period, so the copy stays ahead of the screen's scan and nothing
  tears, and pictures come at an even 30 a second. Streamed pictures that haven't changed aren't drawn again.
- **Disc.** `port/ds/discfile.c` reads the .bin directly: disc label, path table, directories. No extraction step.
  `ds_prefix.h` points `fopen` and friends at it, so `os9.c` and `cdrtos.c` are unchanged. It reads 8 sectors at a time.
- **Audio.** `port/ds/audio_ds.c` decodes ADPCM on the ARM9, from ITCM, into a looping ring the sound hardware plays at
  the sector's own rate (37.8 or 18.9 kHz), so nothing needs resampling.
- **Memory.** The program stays at 0, the data moves to 0x20000 and the heap to 0x28000. The program's first long is
  changed to point at the OS-9 system globals, which is how the game finds them.
- **Stack.** The game runs on a 128 KB stack in main RAM, because the DS's own is 16 KB.
- **Saves.** The NVRAM is a block of memory, copied to `/hotelmario/saves/hotel_mario` whenever it changes. Without an SD
  card a save lasts until the power goes off. At start-up the game creates its save file if there isn't one, and it
  reports any failure there as "your CD-i player's memory is full". That message never meant the DS was short of memory:
  it appeared when melonDS had no SD card (DLDI off), at start-up, so in demo mode as well as in a normal game.
- **Bottom screen.** Controls and credit, like the 3DS. SELECT shows the messages and the speed line.

## What Musashi still runs

| Code | Size | When |
|---|---|---|
| `cdi_bumper` | 12 KB | the Philips logo |
| `intro_sub.o` | 17 KB | the intro |
| `mario_set_sub.o` | 40 KB | stages |
| `L*_s*_sub.o`, `L*_s*_subT.o` | 172 modules with the two above, 10 MB in all | each stage's own code, streamed from the disc |

Musashi costs about 850 KB of the DS's memory (270 KB code, 576 KB tables). It's the first thing to replace if room runs
out: a smaller interpreter, or recompiling the modules that matter.

## Checked

- The disc reader, against every extracted file: 61 files, 339 MB, byte for byte.
- A PC build with the DS memory layout, disc reader, display code and fast interpreter hook, and a stand-in for the DS
  video hardware: Philips logo, Fantasy Factory, intro cartoon, title, menu and Stage 1, drawn at 256 x 192.
  Two 90-second runs, no crashes.
- `tools/build_ds.py` links, and the shared files still compile for PC and 3DS.
- In melonDS, the `.nds` boots and plays, the Philips logo is smooth, and the zoom and text look right.
- In the same PC build: the views in the menus and in a stage, and the save file: written to the card, and loaded by the next
  boot. With nowhere to write, the "memory is full" screen is gone.

The PC testing found three bugs, all fixed:
- The logo was black: one plane's brightness was applied to the whole screen.
- The lower part of a still was missing: the change check didn't sample line starts.
- A crash when a stage started: the game closes a file the stream is still reading.

## Not checked

- Anything on real DS hardware. melonDS shows the code works, not how fast it runs on a real console.
- Real speed. The table above is an estimate: read the real numbers with SELECT in a stage.
- Sound, on the DS side: no result reported yet.
- Stages other than Hotel 1 Stage 1, and memory use across a whole game: the allocator never reuses memory, and only
  1 MB of heap is available.

## Testing it

The easy way: `python tools/build_ds.py --full` makes `build/ds/HotelMario_full.nds`, with your disc image inside it.
Open that one file in melonDS or put it on a flashcart. Saves stay on the SD card; in melonDS, turn on DLDI to keep them
after closing it.

The small way, for quicker rebuilds:

1. In melonDS, turn on DLDI (DS mode) and sync its SD card to a folder.
2. Put your disc image in `hotelmario/` in that folder (any name ending in `.bin`).
3. Open `build/ds/HotelMario.nds`.

You should see the Philips logo, then Fantasy Factory, then the intro. The bottom screen shows the controls. SELECT shows
the messages and the speed line there: in a stage, near 30 pictures a second at about 6 ms each means the
estimate holds. d-pad moves, A and B are the CD-i buttons, X turns the stage zoom on and off, START + SELECT quits. If the game
stops, the reason appears on the bottom screen by itself.

Both `.nds` files contain code recompiled from your own disc, and the full one holds the whole game, so like the 3DS and
Wii builds they must not be shared.
