# Hotel Mario decomp - recon notes

## Direction (decided 2026-10-19)
- **Main goal: a matching decompilation** of `cdi_hotel` (C that rebuilds the original module byte-for-byte),
  then of the streamed code modules. No matching decomp of a CD-i game is known to exist.
- **Port requirement:** booting the port plays everything (Philips logo, intro cutscenes/FMV, menus, game) inside
  the one game window, driven by the game's own code. No external windows, players or pre-rendered videos;
  `tools/cdi_fmv.py` (MP4) is an offline decoder check only.
- **Side project: the BIOS-free port** (`port/`) aimed at Wii and 3DS. It reimplements CD-RTOS, so it needs
  only the user's disc. Status (2026-10-19): boots with no input through the Philips logo, Fantasy
  Factory logo and the whole intro cartoon to the title screen, all looking right; the attract demo
  starts after a while, with the Philips jingle, intro speech and title music. Not yet checked: gameplay,
  menus with input; in-game sound-map (`sm_*`) music and effects aren't emulated.
- **Prior art:** [cdirecomp](https://github.com/mstan/cdirecomp) by mstan (R.A.I.D. community) is a CD-i
  static recompiler that runs Hotel Mario into gameplay using a user-supplied CD-i system ROM. It's licensed
  PolyForm Noncommercial, so we don't copy its code; its docs are fine to learn hardware/OS behaviour from.
  It is not a decompilation.

## Target
- **Hotel Mario (Europe)** CD-i, `.bin` SHA1 `8D671F7BC729ED8D6A07D1D40BA48CBC9530BD30`
- Disc label: CD-RTOS, volume "Hotel Mario", created 1994-04-06, prepared by Fantasy Factory
- Reference: USA Beta 1993-11-23, `.bin` SHA1 `183B2EFD1F5B926757A8FB514CC861FDF7A244C5`

## Program
- `cdi_hotel`: one OS-9/68000 program module, 112,984 bytes, edition 7
  - exec 0x52, data size 0x2600, stack 0xC00, initialized data at 0x1B940
- Compiled C: `_cstart`, `_stkcheck`, and `link a5 / unlk a5; rts` frames (`N]Nu`).
  This looks like the **old Microware OS-9/68000 C compiler** (K&R, a5 frame pointer), not Ultra C. Still needs confirming.
- `cdi_hotel.stb`: the **original debugger symbol table** (558 names), e.g. `init_doors`, `check_doors`,
  `elev_switch`, `jump_coll_det`, `luigi_hat_spit`, `cd_play`, `alphablit`, `after_the_game`.
- `cdi_hotel_data` (4 KB) and `cdi_bumper` (12 KB) are other small modules (not yet inspected).
- Prototype `cdi_hotel` is 129,024 bytes: an earlier build with a different code layout, so it's useful for comparison.

## Symbols (`tools/stb.py`)
- `.stb` is an OS-9 data module. At 0x44 is the record offset and at 0x48 the count (564).
  Each record is `u32 value, u16 flags, u32 name_offset`.
- Flag bit 0x0004 marks code (484 functions, value = module offset). Other flags mark data, as a6-relative offsets biased by 0x8000.

## Compiler evidence
- `link.w a5,#n` frames and a `movem.l` register save; the first two arguments go in **d0/d1** and the rest on the stack.
- Globals are accessed via **a6 biased by 0x8000**. Calls go through `jsr -x(a6)` (a jump table, `_jmptbl`).
- These match the Microware OS-9/68000 C calling convention.

### Confirmed: Microware OS-9/68000 C compiler 3.2 (from the official 3.2 release notes, ICDIA `os9ccmp.pdf`)
- The release notes list 12 C functions that were new in 3.2. Eight of them are linked into `cdi_hotel`:
  `_lcalloc`, `_lfree`, `_lmalloc`, `_lrealloc`, `_mallocmin`, `memmove`, `strtol`, `strtoul`. So it was built
  with the 3.2 libraries. Together with the pre-Ultra-C library names below, that means **C 3.2**.
- Official 3.2 tool editions (module edition / CRC), to check that a copy of the compiler is the right one:

  | module | edition | CRC |
  |---|---|---|
  | cc | 44 | 033985 |
  | cpp | 41 | 30C434 |
  | c68 | 357 | 180F6E |
  | o68 (optimizer) | 19 | 0E66EF |
  | r68 (assembler) | 67 | DF2223 |
  | l68 (linker) | 64 | A3F33B |

  (CRCs are OCR'd from the scan, so read them as close-but-unverified.)

### Earlier evidence: Microware OS-9/68000 C compiler 3.x, not Ultra C
- The linked library is the 3.x-era `clib.l`. It uses the old OS-9 call names (`_ev_creat`, `_ev_wait`, `_mkdata_module`,
  `intercept`, `os9fork`, `_srqmem`, `_sysdbg`, `_utinit`, `_dumprof`) where Ultra C uses `_os_*`.
- Float/long helpers are the `_T$...` family (`_T$DAdd`, `_T$LtoA`, `_T$FtoD`, `PackD`, `NormD`), from the 3.x `math.l`.
- CD-i's CD-RTOS is OS-9/68K **2.4**. The C compiler of that era is 3.2, while Ultra C shipped with OS-9 3.0
  (see the ICDIA documentation index: https://icdia.co.uk/microware/index.html).
- Game code makes **no `_stkcheck` calls**, so it was built with stack checking off (`cc -s`). 356 of 481 functions use `link.w a5`.
  The library functions mostly don't, which suggests hand-written assembly.
- Exact version and flags (`-s`, optimizer `o68` on/off) can only be confirmed by compiling test functions with the
  real compiler (cc/c68/o68/r68 running under OS-9, e.g. on an emulated CD-i 605 dev player) and diffing against `asm/`.
- Until then: write C that's non-matching but equivalent, while learning code patterns by hand.

## Bring your own disc
- `python -I tools/setup.py "<your Hotel Mario (Europe).bin>"` checks the image SHA1, extracts it, generates `asm/` and runs the matching build.
- Recognised images: EU (supported) and the 1993-11-23 USA beta (recognised, not built yet). No game data goes in git.

## Initialised data and the jump table
- `cdi_hotel` has an empty IData block. Its initialised data ships as the separate data module `cdi_hotel_data`:
  at 0x44 is a u32 data offset (0x1880) and a u32 size (0xd80), then the bytes. Relocation lists follow.
- `_jmptbl` (data 0x231e, called as `jsr -$5ce2(a6)` etc.) is a run of `jmp abs.l` entries in that data. gen_asm reads it and
  comments each call with its target (`-> fwrite`).
- Long calls inside the code use `move.l #off,d0; jsr d8(pc,d0.l)`, which gen_asm also resolves.

## Decompiling
- `src/*.c` holds K&R C for game functions. Check it with `gcc -std=gnu89 -fsyntax-only -Iinclude src/*.c`.
- With no Microware compiler yet, the build keeps using the asm. C counts as done when it does the same thing as the asm.
- Game code is 0x36a-0x17122 (208 functions). 34 of them are hand-written asm (no `link.w a5`) and count as done as-is.

## Module container (`tools/os9mod.py`)
- Header parity (inverted XOR of the words in 0x00-0x2D) and the module CRC (CRC-24, poly 0x800063) recompute **exactly** for `cdi_hotel` and `.stb`.

## Data
- `*.rtf` = real-time files (interleaved video/audio/data sectors), `cdi_*.map` = their indexes.
- Per level N: `LN_dat.rtf` (graphics/level data), `LN_av.rtf` (animation/video), `LN_am.rtf` (FMV + audio).
- `intro.rtf` and `exit.rtf` are the FMV cutscenes; `bumper.rtf` is the Philips logo.

## Tools
- `python -I tools/cdi_fs.py list <bin>` lists the files on the disc.
- `python -I tools/cdi_fs.py extract <bin> build/eu/fs` extracts them. Realtime files are kept as raw 2352-byte sectors.

## Port: how the start-up was made to look right (2026-10-19)

### The Fantasy Factory logo (DYUV decoding)
Nothing was restored or edited: the logo has always been intact on the disc. It's a full-colour **DYUV**
still in `intro.rtf` (video sectors 12-79), shown right after the Philips logo. The stripes came from the
port's DYUV decoder, which got three things wrong:

1. **Nibble order.** DYUV codes a pair of pixels in two bytes: `U delta | Y0 delta`, then
   `V delta | Y1 delta`. The **high nibble is the colour (chroma) delta and the low nibble the
   brightness (luma) delta**. The port had them the other way round.
2. **Wrap-around.** The deltas (table `0, 1, 4, 9, 16, 27, 44, 79, -128, -79, -44, -27, -16, -9, -4, -1`)
   are added with **8-bit wrap-around (mod 256)**, like the chip's adders, and the encoder relies on it.
   The port clamped to 0-255, so once a line wrapped, everything after it on that line was off. That gave
   the horizontal stripes.
3. **Line start values.** Each line starts from the Y/U/V values in the video chip's DYUV start registers
   (DCP instructions `0xCA` plane A, `0xCB` plane B, data `Y<<16 | U<<8 | V`), not from fixed numbers.

Fixes are in `port/display.c` (`draw_line`, and `instr` for `0xCA`/`0xCB`).
To check: `hotel_dev2.exe --headless --dump-at 16000`, which writes `build/port/frame_both.rgb`.

### Getting past the logo (the OS-9 tick counter)
The game times its waits by reading the OS-9 tick counter straight from memory, not through a system
call. `cdi_hotel` reads `*(*(long *)_sysglob + 0x54)`, where `_cstart` set `_sysglob = *(long *)0`. The
streamed code modules read `*(*(long *)0 + 0x54)`. Both agree only if the system globals' first long
points back to the globals. The port builds that block (`os9_tick` in `port/os9.c`), at the address the
long at 0 points to, and updates the counter at 100 ticks/s.

### The end of the intro (display signals)
`cd_nrl` asks the video driver for a signal at the next field (`dc_ssig`, signal code in **d3**) and spins
until the handler sets `gds+0x498`. The port had read d4 and repeated the signal every field; now it's a
one-shot (`once_signal`).

### Characters over backgrounds, title logo (plane transparency)
A CD-i has two image planes. Plane A (front: cartoon, menu text) is see-through wherever its pixel equals
a **key colour**, so plane B (painted backgrounds, the HOTEL MARIO logo) shows behind it, like a green
screen. Register `0xC1` holds the rule per plane: A bits 0-3, B bits 8-11. Bits 0-2 are 0 always,
1 colour key, 2 RGB555 transparency bit, 3/4 matte flag 0/1, 5/6 matte flag or key; bit 3 inverts, so 8
means never transparent. The game uses `0x800805`: A by key, B never.
The key is compared ignoring the mask register (`0xC7`/`0xC9`) and the low 2 bits of each component.
The game's `set_up_clut` writes `0xC4 + palette colour 0` into **the table of the plane whose palette it
loads**, so the key belongs to the plane whose table runs the instruction. The port used to send every
`0xC4` to plane A, so plane B's sky colour kept overwriting A's key.

### Sound: which audio is played and which goes to RAM
`PCB_AChan` (the manual calls it the "audio to memory selection mask") means the **audio processor's**
memory: audio sectors on those channels are played. Audio sectors on channels selected in `PCB_Chan` but
not in `AChan` go to the program's RAM through the audio PCLs. The Philips logo program proves it: it plays
with `AChan = 0x8000` and no audio PCL for channel 15, and its jingle is heard on a real player. The port
had it the other way round, so nothing was heard. With the fix:
- Philips logo jingle: `bumper.rtf` channel 15, Level B stereo (149 sectors).
- Intro speech: `intro.rtf` channel 15, Level C mono (18.9 kHz), sectors 133-3782.
- Title music: `intro.rtf` channel 15, Level B stereo (37.8 kHz), from sector 3904.
  `intro.rtf` also has three more Level B channels (12-14) from sector 7440.
- In-game music goes the other way: `cd_smap` streams audio into a one-sector **sound map** in RAM
  (`init_audio`: `sm_creat(path, 3 = Level B stereo, 0x12 groups, &addr)`, `sd_loop(..., -1)` loops it for
  ever). `cd_mix` starts it with `sm_out` on the first buffer and then ORs sound effects (`cd_xbuf`) into
  it. The port doesn't emulate sound maps yet.
- `--wav <file>` records everything played (44.1 kHz stereo), also with `--headless`.

### Controls, menus and gameplay (2026-10-20)
- **Pointer = joystick.** The CD-i thumbpad moves a pointer in a 768x560 space. The game parks it with
  `pt_pos` and reads movement with `pt_coord`: the title menu centres it at (383, 100) and counts y past
  150 / under 50 as a step; gameplay centres it at (383, 234), and each read (every 3rd frame) turns
  "x moved" into a direction code in the x field (1 = left, 2 = right) that Mario's logic uses until the
  next read. `pt_coord` returns **x in the high word, y in the low word** (the clib binding stores the low
  word through the y pointer). Swapped, the menu always fell through to DEMO.
- **sprintf.** The recompiled C-library formatting (printf/fprintf/sprintf, hand-written 68000 with jump
  tables) dropped every conversion: `sprintf("L%d_am.rtf", 1)` gave `Ld_am.rtf`, so a new game couldn't open
  its level and showed YOUR DISC MAY BE DIRTY. Those three run on the interpreter for now; the recompiler
  bug itself is still to find.
- **NVRAM.** `gs_free` (GetStt 0x43) must report free space (8 KB minus the save file), or the game says
  the player's memory is full.
- **Nested modules.** A signal handler can call into one streamed module while another is running on the
  interpreter; Musashi's context is saved and restored around the inner call.
- **Frame timing.** The window shows a new picture only after the game finishes one (it waits for the
  next field); drawn at other moments it caught sprites half erased.

### Frame timing and smoothness (2026-10-20)
- The game logic runs 50 updates a second (PAL); Mario walks 2 pixels per update (100 px/s).
- **When to show a frame.** The window shows a new picture only when the game has finished one; drawn at
  other moments it caught sprites half erased. Finished means it waits for the next field: `line_event`
  (`wait_next_field`) in menus and cutscenes, the **per-field display signal** in the stages. Counting only
  the first made the stages show ~10 pictures a second - that was the choppiness.
- **Smooth pacing** (settings, on by default): fields and field signals come every k monitor refreshes
  (k = refresh / game speed, rounded), so every frame stays on screen equally long. 144 Hz: k = 3, 48 fields
  a second (47-48 new pictures a second measured in play); 60 Hz: 60.
- **Game clock.** Dragging the window freezes the main loop on Windows (and the settings menu pauses it);
  the disc stream, field timer and OS tick counter run on a clock that stops during such stalls, so
  cutscenes carry on instead of racing through seconds of "overdue" sectors.
- Run-length detection needs run markers in the data (bit 7); the stage's sprite layer sits on an old
  cutscene buffer and must not be treated as run-length (it flickered).
- Picture crop "TV" = lines 2-240 (line 241 already shows the strip below the pictures, a 1-pixel line at the bottom); the title and stages add 20
  lines of PAL-only art below.
- **Fades read the screen brightness first.** `fade`/`unfade` call `dc_rdfi` (SetStt 0x56 sub 3) on FCT
  slot 4 and skip the fade when the screen is already black / lit. Unanswered, a second fade ran from full
  brightness over a black screen whose planes were mid-switch: static before the intro text and a lone
  shifted hotel logo before each stage card. The port now answers dc_rdfct/rdfi/rdlct/rdli from its tables.
- Hotel map fade-in: the selected hotel's highlight (plane A, brightness 0, overlay mode) shows as a black
  tree shape for ~0.5 s until the map turns mixing on. That follows the register writes and MCD212 rules
  (A in front, brightness applies in overlay), so it is probably what a real player shows too.

### Ending and credits (2026-10-20)
- Bowser's hotel is game index 8 (15 stages; index 6 is never used). Winning its last stage runs
  `exit_hm(1)`: the ending animation, then the save screen; EXIT leads to the high scores and `exit_hm(0)`,
  the scrolling credits (also after a game over), then the program exits.
- The credits wait one field per line (`dc_ssig` + `sleep(10)`). The library's `sleep` falls through into
  `tsleep`; without that fall-through the credits raced past in seconds. F$Sleep with bit 31 set counts
  1/256 s.
- Test without playing: a save for hotel 8 stage 0x0e (`08 0f` at +0x34), RESTORE, and
  `HM_POKE=ms:20087e=0;ms:20057a=0` (Bowser phase off, no open doors) wins the stage.

### Saving (2026-10-20)
- `/nvr/hotel_mario` (one 62-byte record: three-letter name at +0x24, hotel/stage at +0x34) lives in
  `saves/`. Hold Down + button 2 on a stage card to save; RESTORE under PLAYER 1 lists the saves.

### Debugging aids
- `--dump-every <ms>` writes `build/port/frames/<ms>.rgb` plus `_A`/`_B` (each plane alone);
  `python -I tools/contact_sheet.py [out.png] [cols] [_A|_B]` makes a timeline sheet.
- With `--trace`: GetStt changes with the stack and the last interpreted instructions,
  transparency/key changes, region values. `HM_WATCHCLUT=<n>` logs writes to palette entry n and n+128
  (`999` = all of plane A's half).

## Third-party code used by the port (not in git, fetched into tools/ext)
- Musashi 68000 core, (c) 1998-2002 Karl Stenerud, MIT licence - runs the code modules streamed from the disc.
- SDL 2.32.10, zlib licence - window, input.
