# Hotel Mario decomp - recon notes

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

### Verdict: Microware OS-9/68000 C compiler 3.x (most likely 3.2), not Ultra C
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

## Module container (`tools/os9mod.py`)
- Header parity (inverted XOR of the words in 0x00-0x2D) and the module CRC (CRC-24, poly 0x800063) recompute **exactly** for `cdi_hotel` and `.stb`.

## Data
- `*.rtf` = real-time files (interleaved video/audio/data sectors), `cdi_*.map` = their indexes.
- Per level N: `LN_dat.rtf` (graphics/level data), `LN_av.rtf` (animation/video), `LN_am.rtf` (FMV + audio).
- `intro.rtf` and `exit.rtf` are the FMV cutscenes; `bumper.rtf` is the Philips logo.

## Tools
- `python -I tools/cdi_fs.py list <bin>` lists the files on the disc.
- `python -I tools/cdi_fs.py extract <bin> build/eu/fs` extracts them. Realtime files are kept as raw 2352-byte sectors.
