# Video script: "Decompiling Hotel Mario"

A narrated video, about 12-14 minutes (roughly 2,000 words of narration at a calm pace). Every fact is
from [TECHNICAL.md](TECHNICAL.md), which is sourced from the repository's notes and git history.

**About visuals.** Suggestions are in *[VISUAL: ...]* lines. Most are things you can make without
any game art: diagrams, code, hex dumps, the terminal, the git log, the repository's site. Anything that
shows the game itself (title screen, cutscenes, Mario in Stage 1) is marked **[game footage]**: that's
copyrighted art, so it's your call whether and how much to show (short clips with commentary, your own
capture from your own disc).

---

## 1. Cold open (0:00-0:45)

*[VISUAL: black screen, then the port window opening. **[game footage]** Philips logo, Fantasy Factory
logo, a few seconds of the intro, then Mario walking in Stage 1, music playing.]*

> This is Hotel Mario, on the Philips CD-i, from 1994. And this isn't a CD-i. It's a PC, running the
> game's own code, with no CD-i BIOS at all. Just the disc.
>
> A couple of days ago this got stuck on a logo. To get here, I had to find out how this machine really works,
> because a lot of it, as far as I can tell, has never been written down. So let's write it down.

*[VISUAL: title card "Decompiling Hotel Mario".]*

---

## 2. The machine (0:45-2:45)

*[VISUAL: a simple block diagram, built up piece by piece: CPU, video chip with two stacked planes,
disc drive, audio, RAM.]*

> A CD-i is a small computer built around a CD drive. The CPU is a Philips 68070: a member of the
> Motorola 68000 family, the same family as the Mega Drive and the Amiga, running at about 15 megahertz.
>
> The interesting part is the video chip, the MCD212. It has no tiles and no sprites. The only moving
> thing it draws for you is the mouse pointer, and Hotel Mario switches that off. Everything else is
> pictures in memory.
>
> What it does have is two image planes, one in front of the other. Plane A can be see-through
> wherever its pixels match a key colour, like a green screen, so plane B shows behind it.

*[VISUAL: two transparent layers sliding apart in 3D, a key colour turning see-through.]*

> Each plane can use a different kind of picture. CLUT: every byte picks a colour from a palette.
> DYUV: full-colour photos, stored as small changes in brightness and colour from one pixel pair to the
> next. And RL7, run-length: long runs of one colour, which is how cartoon animation streams off the
> disc.
>
> And you don't program this chip by poking registers. You give it two little programs: one it runs
> every frame, the field control table, and one it runs line by line as it draws, the line control
> table. Change a palette halfway down the screen? Put an instruction on that line.

*[VISUAL: a screen raster drawing top to bottom, with small instruction labels firing at certain lines:
"load palette", "plane on", "brightness".]*

> Then the disc. CD-i discs interleave video, audio and data sectors in one file, each tagged with a
> channel. The drive reads 75 sectors a second and drops each one into memory where the program asked.
> Video lands straight in the memory a plane is showing. Audio is 4-bit ADPCM, and it can either play
> straight from the disc, or sit in RAM as a "sound map" that the program can change while it plays.

*[VISUAL: a sector stream as coloured blocks (V, A, D) flowing from a disc into labelled buffers.]*

---

## 3. The software (2:45-4:30)

*[VISUAL: hex dump of an OS-9 module header, with the sync bytes, name and CRC highlighted. Generic
illustration, not the game's bytes, or the repository's own tool output.]*

> The CD-i's operating system is CD-RTOS, a version of Microware's OS-9. Hotel Mario's main program is
> one OS-9 module, `cdi_hotel`: about 113 kilobytes. It was written in C, and I could pin down the
> compiler: Microware's OS-9 C compiler, version 3.2. Microware's own release notes list twelve
> functions that were new in 3.2, and eight of them are linked into the game.
>
> Even better, the disc still has the original debugger symbol table: 558 names. `init_doors`.
> `check_doors`. `elev_switch`. `luigi_hat_spit`. And one the programmers censored themselves:
> `get_the_fxxxxxx_bonus`.

*[VISUAL: the symbol list scrolling, highlighting those names.]*

> But here's the twist. The main program is about thirty-five thousand instructions. The whole game is
> over two million. Almost all of the code isn't in the program at all. It arrives through the data
> channels of the streams: 172 code modules, about 14 megabytes, one pair for every stage of every
> hotel.

*[VISUAL: counter animating 35,000 then 2,160,000; a stream of "D" sectors assembling into module
blocks.]*

> And two-thirds of those instructions are pictures. Compiled sprites: a sprite is a long run of "store
> this value, store this value, skip to the next row". To draw it, you run it.

*[VISUAL: a mock compiled sprite: `move.l #$..,(a1)+` lines next to a small grid filling in, using
made-up colours, not real game art.]*

---

## 4. Why it feels stiff (4:30-5:45)

*[VISUAL: **[game footage]** Mario walking and turning, slowed down; or a diagram of frames on a
timeline with every third one highlighted.]*

> People say Hotel Mario feels stiff. The code explains why.
>
> The CD-i has no D-pad register. The controller moves a pointer. So in a stage, the game parks the
> pointer in the middle, and every third frame it checks which way it moved, turns that into "left" or
> "right", and parks it again. Mario follows that until the next check. A press can take three frames
> to register.
>
> And there's no sprite hardware, so the CPU does everything: copy the background back over where Mario
> was, then run his sprite code at the new spot. For a standing Mario that's a 32 by 31 pixel block,
> fifty times a second, on a 15 megahertz CPU. Every animation frame is more code to stream from the
> disc and keep in memory, so the animations are short. Mario walks exactly two pixels a frame.
>
> It's not broken. It's a game built around what the machine could do.

---

## 5. A matching decompilation (5:45-7:00)

*[VISUAL: terminal: `tools/build.py` assembling and printing the matching SHA1. Then the progress site's
"Road to 100%" meters.]*

> The main goal of this project is a matching decompilation: C source that rebuilds the program byte
> for byte. As far as I know, nobody has done that for a CD-i game.
>
> Step one: read the disc's file system. Step two: split the program into functions using the original
> symbols. Step three: disassemble it, re-assembling every single instruction to check it gives back the
> original bytes. Then rebuild the OS-9 module, header parity and CRC included, and compare the SHA1.
> It matches.
>
> Then the slow part: rewriting functions in C, one at a time, checked line by line against the
> assembly.

*[VISUAL: split screen: assembly of `movi_stan` on the left, its C on the right.]*

> Along the way you find things. The holiday messages on the loading screen use date ranges. The
> owner list on the loading screen goes hotel 1 to 8, but has nothing for hotel 6, and there are no hotel
> 6 files on the disc: Iggy is the only Koopaling who isn't named. That's speculation, but it's a nice
> one.

---

## 6. The port: replacing the operating system (7:00-8:15)

*[VISUAL: architecture diagram: the game's recompiled main program and the Musashi interpreter on top;
underneath, "OS-9 calls", "CD-RTOS disc", "MCD212 display", "ADPCM + sound maps"; at the bottom, SDL2
and the PC.]*

> The side project is a PC port. Not an emulator that boots a CD-i BIOS: the port replaces the
> operating system. It answers the game's OS-9 system calls itself, it plays the disc streams itself,
> and it draws the video chip's planes itself.
>
> The main program is translated from 68000 machine code into C ahead of time and compiled into the
> executable: 579 entry points, about forty thousand instructions. But the 172 streamed modules show up
> while the game runs, so those go to Musashi, a 68000 interpreter, sharing the same memory.
>
> And then you press run, and it doesn't work. For a long time. Here's what was wrong.

---

## 7. The bugs (8:15-12:30)

*[VISUAL: for each bug, a "case file" card with the symptom, then the fix. Git log lines from the
commits make good transitions.]*

### Stuck on the logo: the clock (about 30 s)

*[VISUAL: a frozen logo, then a pointer diagram: address 0 points to the system globals, whose first
long points back to itself, with +0x54 = ticks.]*

> First, the game froze on the Fantasy Factory logo. It wasn't waiting for the disc. It was waiting for
> time. The game reads the OS-9 tick counter straight out of memory. The main program goes through a
> pointer it saved at start-up; the streamed modules go through address zero. Both only work if the
> system globals start with a pointer to themselves. Build that, tick it a hundred times a second, and
> the logo moves on.

### The display signal (about 15 s)

> Then the end of the intro hung, because the game asks for one signal at the next frame, and the port
> was reading the request from the wrong register and repeating it every frame.

### Floating characters: transparency (about 30 s)

*[VISUAL: two planes, keys being written to the wrong plane, then the right one.]*

> Next, the intro characters didn't sit on their backgrounds, and the HOTEL MARIO logo was missing. Each
> plane has its own transparency rule, and the key colour belongs to the plane whose table loads it. The
> game writes a key next to every palette. The port was sending them all to plane A, so plane B's sky
> colour kept wiping out plane A's key.

### Stripes: DYUV (about 40 s)

*[VISUAL: a synthetic DYUV line decoded wrong (clamped) vs right (wrapping), as a graph of brightness
across the line. Avoid using the logo itself unless approved.]*

> The Fantasy Factory logo came out in stripes. It was never damaged on the disc. The decoder had three
> mistakes. The two halves of each byte were swapped: the high half is colour, the low half brightness.
> Each line has to start from values the chip is given, not fixed numbers. And the big one: the deltas
> wrap around at 256, like the chip's 8-bit adders, and the picture depends on it. Clamp them, and the
> moment a line wraps, the rest of the line is wrong. That's a stripe.

### Silence: one word in a manual (about 35 s)

> The intro had no sound. Microware's manual calls one field the "audio to memory selection mask". I
> read "memory" as the program's memory. It means the audio processor's memory: those channels are the
> ones you hear. The proof was the Philips logo program, which plays its jingle with exactly that mask
> and nowhere in RAM to put it. Flip the meaning, and the jingle, the speech and the title music all
> play.

### The menu that always picked DEMO (about 25 s)

> At the title menu, the highlight always slid down to DEMO. The pointer call returns x in the top half
> and y in the bottom half. I had them backwards, so the game saw the pointer far down the screen and
> kept stepping. The pointer also has to live in the CD-i's 768 by 560 space.

### "Your disc may be dirty" (about 45 s)

*[VISUAL: the string `"L%d_am.rtf"` turning into `Ld_am.rtf`; then `moveq #-1` shown as 0x000000FF vs
0xFFFFFFFF.]*

> Then, choosing one player said: "your disc may be dirty". The game builds file names with sprintf,
> "L, percent d, underscore am", and it was getting "Ld_am". Every number had vanished.
>
> The cause was one instruction. `moveq` loads a small number and sign-extends it, so 255 means minus
> one. The disassembler reports it as plain 255, so my recompiler turned every negative `moveq` into a
> positive one. That broke the number formatting, and it also turned Mario's cap and overalls black,
> because his colour change asked for row minus one and got row 255. One fix, both bugs gone.

### Choppy: 10 pictures a second (about 35 s)

*[VISUAL: a frame-time graph going from jagged ~10 per second to flat ~48.]*

> Now Stage 1 ran, but at about ten pictures a second. The port only shows a frame when the game has
> finished one, and it counted "finished" as waiting for the frame event. Menus wait on that. The stages
> wait on a different signal. Counting both: forty-eight pictures a second. Then deliver frames on an
> even beat for the monitor's refresh rate, and stop the game's clock when Windows freezes the window
> during a drag, and it's smooth.

### Silent stages: twenty bytes (about 30 s)

> Last, in-game sound. The game makes a sound map in memory and loops it, the disc streams music into
> it, and the game mixes sound effects in. I built all that, and the stages were still silent. An audio
> sector is 2,304 bytes of sound plus 20 bytes of padding. I copied all of it, and those 20 bytes landed
> right on top of the sound map's descriptor. Copy 2,304, and Hotel Mario has music.

*[VISUAL: **[game footage]** Stage 1 with music, held for a few seconds.]*

---

## 8. Bring your own disc, and credits (12:30-13:30)

*[VISUAL: the README's disc table; the terminal running `setup.py`, `build.py`, `build_port.py`.]*

> None of the game is in this project. No disc image, no extracted files, no game code. You bring your
> own copy, the European release for now, and the tools check it and build everything from it.
>
> Thanks to Karl Stenerud for Musashi, the SDL team, Volker Barthelmann and Frank Wille for vasm, and
> Microware's own manuals. And to mstan, whose cdirecomp already runs Hotel Mario using a real CD-i
> system ROM. Different approach, separate project, and worth a look.

*[VISUAL: credits list as text on screen, with links.]*

---

## 9. Outro (13:30-14:00)

*[VISUAL: the progress site, then the technical page.]*

> The decompilation keeps going, function by function. The whole story, with every register and
> every bug, is written up on the project site. Link below.
>
> Thanks for watching.
