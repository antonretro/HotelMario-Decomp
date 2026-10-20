# Inside Hotel Mario: how a CD-i game works, and how it was decompiled and ported

*Hotel Mario* (Fantasy Factory / Philips, 1994) is the CD-i Mario game: Mario and Luigi close doors in
seven Koopa-run hotels. This write-up explains how the game works inside, on the CD-i's hardware and
operating system, and how this project took it apart: first a byte-for-byte matching rebuild of its
program, then a PC port that needs no CD-i BIOS, only your own disc.

Everything here comes from the project's own work: the disassembly, the decompiled C in `src/`, the port
in `port/`, [NOTES.md](../NOTES.md), [TRIVIA.md](../TRIVIA.md) and the git history. Where something is an
interpretation rather than a measurement, it says so.

Contents:

1. [The machine](#1-the-machine)
2. [The software](#2-the-software)
3. [Why it plays the way it does](#3-why-it-plays-the-way-it-does)
4. [Taking it apart: the matching decompilation](#4-taking-it-apart-the-matching-decompilation)
5. [The BIOS-free port](#5-the-bios-free-port)
6. [The port, bug by bug](#6-the-port-bug-by-bug)
7. [Bring your own disc](#7-bring-your-own-disc)
8. [Credits](#8-credits)

---

## 1. The machine

A CD-i player is a small 68000-family computer built around a CD drive. The parts that matter to
Hotel Mario:

### The CPU: Philips SCC68070

A Philips member of the Motorola 68000 family, running at about 15 MHz. To the game it is a 68000: the
same instructions, the same registers. The port's interpreter is configured as a plain 68000 "like the
CD-i's 68070" (`port/musashi_conf.h`), and that has been enough.

### The video chip: MCD212

The MCD212 is the CD-i's display processor. There is no tile engine and no sprite hardware; the only
moving object it draws for you is the pointer cursor, which Hotel Mario hides (`gc_hide` in
`init_video`, `src/video.c`). Everything else is pictures in memory. What it does have:

- **Two image planes.** Plane A sits in front of plane B, and behind both is a backdrop colour. Hotel
  Mario's screens are 384 pixels wide and 280 lines tall (PAL), one byte per pixel in the stages
  (`0x180` = 384 bytes per line in the decompiled code).
- **Several image codings per plane:**
  - **CLUT** (colour look-up table): each byte is an index into a palette (CLUT4, CLUT7, CLUT8).
  - **DYUV** (delta YUV): full-colour stills. Each pair of pixels is coded as small *changes* in
    brightness (Y) and colour (U, V) from the previous pair. Hotel Mario uses it for photographic
    pictures such as the Fantasy Factory logo.
  - **RL7** (run-length, 7-bit): a byte with bit 7 set starts a run of one colour. This is how the
    cartoon animation is streamed: the frames compress well and decode as fast as the screen is drawn.
- **Transparency.** Plane A can be see-through wherever its pixel equals a **key colour**, like a green
  screen, so plane B shows through. A register selects the rule for each plane (never, colour key, a
  transparency bit in RGB555 pixels, or a matte), and the rule can be inverted.
- **Mixing.** Instead of laying A over B, the chip can *add* the two planes, each weighted by its own
  brightness. That is how a cross-fade is done in hardware.
- **Control tables instead of registers you poke.** The chip runs two small programs every frame:
  - the **field control table** (FCT), once per field;
  - the **line control table** (LCT), entry by entry as it draws each line.
  Their instructions load palette entries, choose a plane's coding, move a plane's start address, set
  transparency, set brightness, and so on. Changing a value halfway down the screen just means putting
  an instruction in that line's slot. The game builds these tables through the CD-RTOS video driver
  (`dc_wrfct`, `dc_wrli`, `dc_flnk` and friends), not by writing hardware registers.
- **Field signals.** The driver can tell the program when a field starts: as an event (`line_event`,
  which the game waits on in `vBlankWait`) or as an OS-9 signal sent once (`dc_ssig`) or every field.

### The disc controller: CDIC, and real-time files

The CDIC reads the disc at 75 sectors a second. CD-i discs mix several kinds of sector in one file,
interleaved, each tagged with a **channel number** and a type (video, audio or data). Hotel Mario's
streams are these **real-time files** (`*.rtf`):

| File | Contents |
|---|---|
| `bumper.rtf` | the Philips logo |
| `intro.rtf` | the Fantasy Factory logo and the intro cartoon |
| `exit.rtf` | the ending |
| `LN_dat.rtf` | hotel N's graphics and level data |
| `LN_av.rtf` | hotel N's animation and video |
| `LN_am.rtf` | hotel N's cutscenes and audio |

A program starts a stream with a **play control block** (PCB): which channels to take, which audio
channels to send where, a signal to receive when the play ends. For each channel a chain of **play
control lists** (PCLs) says where in memory the sectors go. When a buffer fills, the drive moves to the
next PCL and can signal the program. This is how video arrives straight into the memory a plane is
showing, and how code arrives too (section 2).

### Audio: ADPCM and sound maps

CD-i audio is 4-bit ADPCM in two qualities that Hotel Mario uses: **Level B** (37.8 kHz) and **Level C**
(18.9 kHz), mono or stereo. Audio reaches the speakers in one of two ways:

- **Straight from the disc.** Audio sectors on selected channels go to the audio processor and play as
  they arrive: the Philips jingle (Level B stereo), the intro speech (Level C mono, 18.9 kHz) and the
  title music (Level B stereo).
- **Through a sound map.** A sound map is a buffer of ADPCM sound groups in the program's own memory,
  which the audio processor plays (`sm_creat`, `sm_out`) and can loop (`sd_loop`). Because it's in RAM,
  the program can change it while it plays. Hotel Mario's in-game music works this way (section 2).

---

## 2. The software

### Written in C, for OS-9

The CD-i's operating system is **CD-RTOS**, a version of Microware's **OS-9/68000** (2.4) with drivers
for the video chip, the disc and the audio. Programs are OS-9 *modules*: a header, code, a checksum.

Hotel Mario's main program is one such module, `cdi_hotel`: 112,984 bytes, edition 7. It was written in
**C** and built with **Microware's OS-9/68000 C compiler, version 3.2**. The evidence:

- Functions use the old Microware calling convention: the first two arguments in `d0`/`d1`, the rest on
  the stack, a `link.w a5` frame, globals reached through `a6` biased by `0x8000`, library calls through a
  jump table (`_jmptbl`).
- The linked C library uses the pre-Ultra-C names (`_ev_creat`, `_mkdata_module`, `os9fork`, `_srqmem`)
  and the 3.x maths helpers (`_T$DAdd`, `_T$LtoA`).
- Microware's 3.2 release notes list 12 C functions new in 3.2. Eight of them are linked into
  `cdi_hotel` (`_lmalloc`, `memmove`, `strtol` and others), so it was linked against the 3.2 libraries.
- The game code never calls `_stkcheck`, so it was compiled with stack checking off. The 1993 prototype
  has 45 such calls: a debug setting turned off for release.

Of the program's 208 game functions, 34 were **written directly in assembly** by the original team:
fast screen copies (`dm`, `ddm`, `rm_lu`, `alpha_move`, `move_screen`) and the random number generator
`rnd`, a 32-bit shift register that rotates left and, when a bit falls out, flips bits 5 and 17.

The disc even carries the original **debugger symbol table**, `cdi_hotel.stb`, with 558 names:
`init_doors`, `check_doors`, `elev_switch`, `jump_coll_det`, `luigi_hat_spit`, and one that the
programmers censored themselves, `get_the_fxxxxxx_bonus`.

### Most of the code isn't in the program

The main program has about 35,000 68000 instructions. The game as a whole has far more, and almost all
of it arrives **through the data channels of the real-time files**: 172 different OS-9 code modules,
about 14 MB and 2.16 million instructions. There is a `Ln_sNN_sub.o` and a `Ln_sNN_subT.o` for every
stage of every hotel, plus `intro_sub.o` and `mario_set_sub.o`. The game streams a stage's modules into
memory and calls them.

About two-thirds of those instructions are **compiled sprites**: pictures stored as code. A sprite is a
long run of `move.l` / `move.w` / `move.b #value,(a1)+`, with an `adda.l #skip,a1` between rows. Drawing
the picture means running it: no decoding, no checking for transparent pixels, just stores. The rest is
ordinary compiled C (around 10,000 functions), so per-stage game logic lives in the modules too.

### How a frame is drawn

A stage screen is two planes. The painted hotel background and the sprite layer are CLUT pictures, and
plane A's key colour lets plane B show through where nothing is drawn. Sprites are moved the
old-fashioned way, by the CPU:

1. **Erase**: copy the background back over where the sprite was. The game keeps a *shadow* copy of the
   rows around the player's floor (`shadow_copy`, `src/mario.c`) and restores a block from it with the
   hand-written copy routine `dm`. For a standing Mario that block is 32 by 31 pixels (`movi_stan`).
2. **Draw**: call the sprite's draw hook, which runs its compiled-sprite code at the new position.
3. **Wait** for the next field, so the chip shows a finished picture.

The player is a small state machine. Each state is a function called once per update with
`(player, game)`: `movi_stan` (standing), and families like `movi_st_*`, `movi_du_*`, `movi_ru_*`,
`movi_tu_*`, `movi_sk_*`, each with a `_fo` (facing forward) and `_ba` (facing back) version.

### Sound

The disc-streamed sound (jingle, speech, title music) plays straight from the drive. In-game music is
different: `init_audio` creates one **Level B stereo sound map of 18 sound groups** and loops it for ever
(`sd_loop(..., -1)`). During a stage the disc streams music into that map a sector at a time (one every
53 ms), and `cd_mix` **ORs the sound effects** (`cd_xbuf`) into the same buffer. The music and effects
reach the speaker as one stream.

### Saving

Saved games go to the player's **NVRAM**, an 8 KB battery-backed file system the game
opens as `/nvr`. The game asks how much space is free before saving, and tells you if the player's memory
is full.

---

## 3. Why it plays the way it does

Hotel Mario is often described as stiff. Reading its code explains a lot of that:

- **Input is read every third frame.** The CD-i has no D-pad register; the thumbpad or mouse moves a
  *pointer*. In a stage the game parks the pointer at the centre of its space (383, 234) and, every third
  update, reads how far it moved (`pt_coord`), turns "moved left / moved right" into a direction code
  (1 = left, 2 = right) and re-centres it. Mario's logic then follows that code until the next read. So a
  press can take up to three frames to register, and directions act in steps. The recorded attract-demo
  input matches: `demoHelp_pt_coord` hands out three values per read.
- **The CPU does all the sprite work.** With no sprite hardware, every moving object costs an erase (a
  memory copy) and a draw (running its compiled sprite), on a 15 MHz CPU, fifty times a second. That
  budget limits how many things can move and how big they are.
- **Few animation frames.** Each animation frame is a compiled sprite: code, in a module streamed from
  the disc into limited RAM. More frames means more code to stream and hold per stage, which pushes
  towards short animation cycles.
- **Fixed steps.** The logic runs 50 updates a second (PAL). Mario walks 2 pixels per update, 100 pixels a
  second.

None of this is a bug. It's a game designed around what the hardware gave it.

---

## 4. Taking it apart: the matching decompilation

The project's main goal is a **matching decompilation**: C source that rebuilds `cdi_hotel` byte for
byte. No matching decompilation of a CD-i game is known to exist.

1. **Read the disc.** `tools/cdi_fs.py` reads the CD-RTOS file system from a disc image and extracts the
   files. Real-time files are kept as raw 2352-byte sectors, because the channel and type tags live in
   the sector headers.
2. **Split the program into functions.** `tools/stb.py` reads the original symbol table (an OS-9 data
   module: 564 records of value, flags, name). Flag bit `0x0004` marks code: 484 functions with their
   offsets.
3. **Disassemble, checking every instruction.** `tools/gen_asm.py` writes one vasm source file per
   function and re-assembles each instruction to confirm it produces the original bytes (98.7% become
   real instructions; the rest stay as data words). It reads the jump table out of the separate data
   module `cdi_hotel_data`, so every `jsr -x(a6)` is labelled with its target (`-> fwrite`).
4. **Rebuild the module.** `tools/os9mod.py` recomputes the OS-9 header parity and the module CRC
   (CRC-24, polynomial `0x800063`); both match exactly. `tools/build.py` assembles with vasm and checks
   the SHA1 against the original.
5. **Swap functions from assembly to C**, one at a time, in K&R C as the Microware compiler expects. A
   Ghidra 10.4 headless setup (with the original symbols, the OS-9 data area, `a6` and the d0/d1 calling
   convention applied) exports a draft per function; the drafts are only a starting point, and each one is
   checked line by line against the assembly. (Ghidra's drafts had the wrong arguments in `cd_init`,
   `seekNplay` and `movi_stan`.)

The C is equivalent to the original but doesn't compile to identical bytes yet, because that needs the
original Microware compiler. The current figures are on the [progress site](https://antonretro.github.io/HotelMario-Decomp/).

Decompiling turned up things nobody had documented: the exact date *ranges* of the holiday messages on
the loading screen, a loading-screen owner list that skips hotel 6 (and the only unnamed Koopaling, Iggy),
an easter-egg system added after the prototype, and functions cut between the 1993 prototype and
retail. They are in [TRIVIA.md](../TRIVIA.md).

---

## 5. The BIOS-free port

The port runs the game on a PC without a CD-i system ROM. Instead of emulating a CD-i and booting its
BIOS, it **replaces the operating system**: it provides the OS-9 system calls and the CD-RTOS drivers
itself, and runs the game's own code on top.

- **The main program is recompiled.** `tools/recomp.py` disassembles every function of `cdi_hotel` (with
  Capstone) and translates the 68000 code into C, which gcc compiles into the executable. The first run
  produced 579 entry points from 39,949 instructions, 99.29% translated. CPU state, big-endian memory and
  the flag helpers are in `port/m68k.h`.
- **Streamed code is interpreted.** The 172 modules arrive from the disc while the game runs, so they
  can't be translated in advance. Calls into code that has no recompiled version run on the
  **Musashi** 68000 interpreter, sharing memory and registers with the recompiled code.
- **The operating system is the port's own.** `port/os9.c` (OS-9 system calls: modules, memory, events,
  signals, files, the NVRAM device), `port/cdrtos.c` (real-time disc playback, PCBs and PCLs, signals),
  `port/display.c` (the MCD212: control tables, planes, codings, transparency, mixing), `port/audio.c`
  (ADPCM) and `port/soundmap.c` (sound maps).
- **SDL2** provides the window, sound, keyboard and gamepad. Keyboard and gamepad drive the CD-i pointer.

The rule from the start: everything plays inside the one game window, driven by the game's own code. No
pre-rendered videos, no external players.

As of October 2026 it boots through the Philips logo, the Fantasy Factory logo and the intro cartoon to
the title screen, the menus take input, and Stage 1 plays smoothly with music and sound effects.

---

## 6. The port, bug by bug

The port was built in about two days. Most of that time went into finding out what the hardware and the
operating system really do, one wrong assumption at a time. In roughly the order they were found:

### Disc streaming and signals

The game starts a play, and its signal handlers count buffers and records as they arrive. The port feeds
sectors at 75 a second into the PCL buffers the game describes and runs the program's own intercept
routine (`F$Icpt`) for each signal.

The first crash was in a data-channel callback reading a garbage descriptor. The port had sent
"channel full" signals made from the disc channel number; the game indexes its table by *entry*, and
each PCL stores the signal to send (at `+0x04`). Sending the stored signal fixed it.

Skipping part of the intro then left the screen black for ever: the port stopped playback on an abort but
never told the game the play had *ended*, so `cd_wait` waited for good. An abort now ends the play the
way a player does, with an end-of-play status and the PCB signal.

### The streamed code

The game's `main` called into memory where nothing had been loaded from the program file. That led to
the discovery that the real-time files carry code modules (section 2), and to running them on Musashi.
Later, a signal handler turned out to call one streamed module while another was already running on the
interpreter, so Musashi's context is now saved and restored around nested calls.

### The display: tables, not registers

The first display pass treated the video calls as register writes, and palettes landed in the wrong
bank. Rewriting it around the real control tables fixed that: FCT and LCT entries are stored as written
and executed every frame and every line. `dc_flnk` chooses which LCT a plane's FCT uses (during the intro
plane B flips between LCT 4 and 5). Each frame the game's FCT turns both planes
*off* (`0xC0` = 0) and its LCT turns them back on at line 2. The intro animation streams RL7 frames
straight into ring buffers in plane B's memory, so the port follows which buffers received video sectors,
and in which coding.

### The Philips logo is a separate program

The logo is its own OS-9 program, `cdi_bumper`, which the game starts with `F$Fork`. The port runs it on
the interpreter with its own data area, sleeps it until its end-of-play signal, and gives it the
per-field display signal (signal 400) that drives its skip check and cross-fade. It waits to fade out
until the PCB's "records left" count reaches 0, so the port counts that down too.

Two smaller logo bugs:

- **Mixing.** The cross-fade into the PHILIPS INTERACTIVE MEDIA card uses the chip's mixing mode
  (transparency-control bit 23 clear: planes added, weighted by brightness). The game itself uses
  overlay.
- **One sector out of step.** When a play ends, the file position moves to where it stopped. The port
  read that position as 2048-byte sectors when the image stores 2352-byte ones, so the second play started
  at sector 14 instead of 13 and every frame of the logo was one sector off (the shield split, the wordmark
  under the globe).

### The tick counter: `_sysglob`

After the logo the game stalled on the Fantasy Factory logo. It wasn't waiting for the disc; it was
waiting for **time**. The game reads the OS-9 tick counter straight from memory, not through a system
call:

- `cdi_hotel` reads `*(*(long *)_sysglob + 0x54)`, where its start-up code set `_sysglob = *(long *)0`;
- the streamed modules read `*(*(long *)0 + 0x54)`.

Both agree only if the first long of the system globals points back to the globals themselves. The port
now builds that block, with that self-pointer, at the address stored at 0, and advances the counter 100
times a second. Later a thread was added to keep it moving, because `cd_mix` spins on it inside a signal
handler.

### Display signals

`cd_nrl` asks the video driver for a signal at the next field (`dc_ssig`) and spins until its handler
sets a flag. The port had read the signal code from the wrong register (`d4`; it's in `d3`) and repeated
it every field. It's a one-shot.

### Plane transparency, per plane

The intro characters didn't sit on their backgrounds and the HOTEL MARIO logo was missing. Two things were
wrong:

- The transparency control register (`0xC1`) holds a rule for each plane: plane A in bits 0-3, plane B in
  bits 8-11. The game uses `0x800805`: A see-through by colour key, B never (and overlay, not mixing).
- The key colour (`0xC4`) belongs to **the plane whose table runs the instruction**. The game's
  `set_up_clut` writes the key right next to each palette it loads. The port sent every key to plane A,
  so plane B's sky colour kept overwriting plane A's key.

The key comparison also ignores the mask register and the low 2 bits of each colour component.

### DYUV: the Fantasy Factory logo

The Fantasy Factory logo came out striped. The logo on the disc was always intact (it's a DYUV still in
`intro.rtf`, video sectors 12-79); the port's decoder had three things wrong:

1. **Nibble order.** A pixel pair is two bytes, `U delta | Y0 delta` then `V delta | Y1 delta`: the high
   nibble is the colour delta and the low nibble the brightness delta. The port had them swapped.
2. **Wrap-around.** The deltas (`0, 1, 4, 9, 16, 27, 44, 79, -128, -79, -44, -27, -16, -9, -4, -1`) are
   added **modulo 256**, like the chip's 8-bit adders, and the encoder relies on it. The port clamped to
   0-255, so once a line wrapped, the rest of that line was wrong: the stripes.
3. **Line start values.** Each line starts from the Y, U and V in the chip's DYUV start registers
   (instructions `0xCA` for plane A, `0xCB` for plane B), not from fixed numbers.

### `PCB_AChan`: which audio is played

The intro was silent. Microware's manual calls `PCB_AChan` the "audio to memory selection mask", and the
port read "memory" as the program's RAM, so it played only the *other* channels. The memory meant is the
**audio processor's**: channels in `AChan` are played; selected audio channels *not* in `AChan` go to the
program's RAM through the audio PCLs. The proof is the Philips logo program: it plays with
`AChan = 0x8000` and no audio PCL for channel 15, and its jingle is heard on a real player. With the
meaning flipped, the jingle, the intro speech and the title music all play.

### The pointer: x, y and the 768x560 space

The title menu always ran its highlight down to DEMO. The menu centres the pointer at (383, 100) and
counts a move of y past 150 or under 50 as a step. `pt_coord` returns **x in the high word and y in the
low word**; the port had them the other way round, so the menu read y = 383 and stepped down every time.
The pointer also has to live in the CD-i's **768x560** space, so the game can centre it at (383, 234) in
play.

### NVRAM

Starting a game said the player's memory was full. `gs_free` (GetStt `0x43`) has to report free space:
8 KB minus the save file.

### `sprintf` and the `moveq` sign bug

Choosing 1 PLAYER then showed YOUR DISC MAY BE DIRTY. The game builds level file names with
`sprintf("L%d_am.rtf", 1)` and got `Ld_am.rtf`: every conversion vanished. At first the formatting
functions were moved onto the interpreter as a workaround.

The real cause was in the recompiler. `moveq` loads an 8-bit immediate **sign-extended** to 32 bits.
Capstone reports `moveq #$ff` as 0xff, so every negative `moveq` became positive. That one bug did two
things: it broke the library's hand-written formatting code (so `%d` disappeared), and
`player_clut_change(-1)` read row 255 of the colour table, so **Mario's cap and
overalls were black**. Fixing the sign fixed both, and the workaround was removed.

### Flickering sprites

Mario flickered on alternate frames. The stage's sprite layer reuses memory that a cutscene earlier
streamed RL7 frames into, so the port went on decoding it as run-length. Forgetting old streams fixed the
sprites but broke the intro backgrounds. The final rule: a picture counts as run-length only if its stream
says so **and** its data has run markers (bit 7 set, which 7-bit CLUT data never has).

### Frame pacing

The stages ran at about 10 pictures a second in the window. The port showed a new picture only once the
game had finished one (drawn at any other moment it caught sprites half erased), and "finished" meant the
game had waited for `line_event`. That's what menus and cutscenes wait on; **the stages sync on the
per-field display signal**. Counting that too gave 47-48 new pictures a second in Stage 1.

Two more things made it smooth:

- **Even pacing.** Fields are delivered every *k* monitor refreshes (*k* = refresh rate / game speed,
  rounded), so every frame stays on screen equally long: 48 fields a second on a 144 Hz monitor, 60 on
  60 Hz. (It's a setting, on by default.)
- **A game clock that stops.** Dragging a window freezes the main loop on Windows. The disc stream, field
  timer and tick counter run on a clock that stops during such stalls, so cutscenes no longer race through
  seconds of "overdue" sectors afterwards.

### Sound maps, and 20 bytes too many

The last piece was in-game sound. `port/soundmap.c` emulates `sm_creat`, `sm_info`, `sm_out`, `sm_off`,
`sm_close`, `sd_loop` and `sc_atten`; each streamed sector is played right after the game's callback has
mixed its effects in, and with no new data the map keeps looping.

The stages were still silent: `sm_out` was being given map id 0. An audio sector carries 2304 bytes of
sound groups followed by 20 bytes of padding, and the port copied all 2324 bytes into memory. The padding
overwrote the map descriptor that sits right after the map. Copying only the sound groups fixed it.

---

## 7. Bring your own disc

This project contains **no game data**: no disc image, no extracted files, no game code, no copyrighted
art. You supply your own copy of *Hotel Mario*, and the tools build everything from it into git-ignored
folders (`build/`, `asm/`).

| Disc image | Status |
|---|---|
| Hotel Mario (Europe), `.bin` SHA1 `8d671f7bc729ed8d6a07d1d40ba48cbc9530bd30` | Supported: matching build and PC port |
| Hotel Mario (USA) | Planned: the port needs the USA program translated as well |
| Hotel Mario (USA) (Beta) (1993-11-23), SHA1 `183b2efd1f5b926757a8fb514cc861fdf7a244c5` | Recognised, used for comparison |

```bash
python -I tools/setup.py "path/to/Hotel Mario (Europe).bin"
python -I tools/build.py
python -I tools/build_port.py
```

`setup.py` checks the image's SHA1, extracts it and generates the disassembly; `build.py` rebuilds the
program and checks it matches; `build_port.py` builds `build/port/hotel.exe`. Full requirements are in
the [README](../README.md).

---

## 8. Credits

- **Musashi** 68000 core, (c) Karl Stenerud, MIT licence. Runs the streamed code modules. Not included;
  fetched into `tools/ext`.
- **SDL2**, zlib licence: window, sound, input. Not included; fetched into `tools/ext`.
- **vasm**, by Volker Barthelmann and Frank Wille: the assembler for the matching build.
- **Capstone**: the disassembler behind the recompiler.
- Microware's *CD-I System Libraries Technical Reference Manual*, for the CD-RTOS structures, and the
  OS-9/68000 C 3.2 release notes, for identifying the compiler.
- **[cdirecomp](https://github.com/mstan/cdirecomp)** by mstan is a separate CD-i static recompiler that
  runs Hotel Mario using a real CD-i system ROM. It's prior art, credited here; none of its code is used.

*Hotel Mario* is © Nintendo / Philips. This project is not affiliated with or endorsed by Nintendo,
Philips or Fantasy Factory. You need your own legally obtained copy of the game.
