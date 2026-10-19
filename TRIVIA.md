# Hotel Mario - trivia and cut content

Found while decompiling. Retail = Hotel Mario (Europe), program built 1994-04-06.
Prototype = USA beta, 1993-11-23. Addresses are offsets in `cdi_hotel`.

## Holiday messages on the loading screen (exact dates from the code)
The messages themselves are already known: the Super Mario Wiki lists them, but as single days
(https://www.mariowiki.com/Hotel_Mario). The decompiled `loading_screen` (0x7d48, `src/loading.c`) shows
the English/Europe build actually uses **date ranges**:

| Shown on | Message |
|---|---|
| Jan 1-5 and Dec 27-31 | HAPPY NEW YEAR! |
| Feb 12-14 | HAPPY VALENTINES DAY! |
| Feb 17 | ITS HOLLIES BIRTHDAY! (the wiki links this to Hollie S. Lohff, play consultant; "Lohff" is also in the disc label's preparer field) |
| May 7-9 | DO NOT FORGET MOTHERS DAY! |
| May 10-30 | SCHOOLS ALMOST OUT! |
| Jun 17-20 | HAPPY FATHERS DAY! |
| Jul 1-4 | HAPPY FOURTH OF JULY! |
| Aug 15-31 | READY TO GO BACK TO SCHOOL? |
| Oct 29-31 | BOOOOOOOOO!!! |
| Nov 17-25 | HAPPY TURKEY DAY! GOBBLE GOBBLE! |
| Dec 11-26 | HAPPY HOLIDAYS! |
| any other day | HERE WE GO! |

Details the wiki doesn't give:
- February 17 is checked **before** Valentine's Day.
- March, April and September have no message.
- The Feb 17 birthday can show in any year; the date is fixed.
- In the game font `g` draws as `!` and `m` as `?`.

None of these messages are in the 1993 prototype.

## The loading screen's owner list skips hotel 6
For stages 1-8 the loading screen names who runs the hotel:
1 Morton, 2 Roy, 3 Larry, 4 Lemmy, 5 Ludwig, **6 (nothing)**, 7 Wendy, 8 Bowser.
Stage 6 has no case in the switch, and **Iggy is the only Koopaling who isn't named**.
Together with the missing `L6_*` files, this points to a sixth hotel, perhaps Iggy's, that was dropped
before November 1993. (That's speculation; nothing for it is left on either disc.)

## A censored function name
The original symbol table has a function called `get_the_fxxxxxx_bonus` (0x7bc). The
programmers censored the swear word themselves in the source.

## Easter eggs ("eegg")
Retail adds an easter-egg system the prototype doesn't have: global `eegg`,
`room_eegg_init`, `eegg_coll_det` (collision with the egg) and `sfb_eegg_det`.
There's also `luigi_hat_spit` (0x23ce), which is retail-only too. Still to do: find out what triggers them.

## There is no hotel 6
Stage files go `L0, L1, L2, L3, L4, L5, L7, L8`, with no `L6`, on both the retail disc and the
1993 prototype. See the owner list above.

## What the prototype-only functions do
Read from the 1993 prototype's disassembly (`build/proto/asm`):
- `close_door_points(game)`: adds **50 points** to the current player's score (`+0x8c4` in a 0x1a-byte per-player record,
  indexed by `+0x6b8`). The prototype scored door-closing with this function; retail doesn't have it.
- `kill_points(a, b)`: **empty**. Points for defeating enemies were planned, but the function was never filled in, even in 1993.
- `evils_die()`: clears both playfield buffers (0x16e00 bytes each) and resets the display line table. Probably the screen wipe
  when the enemies ("evils") are gone. Retail doesn't have it.
- `check_floor_map(x, y)`: looks up a bit in a 23-byte-per-row floor bitmap (`gdsP + 0x630`) and answers "is there floor here?".
  Retail has `init_floor_map` / `is_door_elev` instead.
- `coll_det`: one big collision routine (about 700 instructions). Retail splits it into `mario_coll_det`, `jump_coll_det`,
  `room_coll_det` and the new `eegg_coll_det`.
- `decrement(p)`: just `(*p)--`.
- `dsEORcb`: an empty "end of record" callback for disc streaming.
- `ds_audio_enable(ch)`, `cd_smaps`, `cd_smap_f`: audio-channel and sound-map helpers that retail merged into `cd_smap`.
- The prototype was built **with stack checking on**: 45 `_stkcheck` calls, against 0 in retail. That's a debug build setting
  the team turned off for release.

## Cut between the prototype and retail
- Functions only in the prototype: `evils_die`, `kill_points`, `close_door_points`, `decrement`,
  `check_floor_map`, `coll_det`, `cd_smaps`, `cd_smap_f`, `dsEORcb`, `ds_audio_enable`.
  The names suggest the scoring for closing doors and killing enemies worked differently.
- Only in retail: `puzzle_8`, `loading_screen`, `cd_credits` (a credits sequence), `exit_hm`,
  `shadow_copy`, the "TIME UP" message, and the separate `cdi_bumper` / `bumper.rtf` logo and `exit.rtf` ending video.
- Prototype debug and error text removed from retail: `dispatch buffer crash`, `DISC SEEK ERROR`,
  `DISC PLAY ERROR`, `CLEAN YOUR DISC THEN RESTART GAME`. The prototype disc's volume name is
  `930610`, which looks like a date (10 June 1993).
- The prototype's disc label credits "Goldberg, Lohff, Hunt". Retail adds "Martin".

## Code streamed off the disc
Most of Hotel Mario's code isn't in the program file at all. The data channel of each real-time file streams OS-9
**code modules**, which the game loads into memory and calls: 172 different ones, about 14 MB with 2.16 million
68000 instructions (the main program `cdi_hotel` has about 35,000). There's a `Ln_sNN_sub.o` and a
`Ln_sNN_subT.o` for every stage of every hotel, plus `intro_sub.o` and `mario_set_sub.o`.
(`tools/rtf_modules.py` lists them.)
- About two-thirds of those instructions are **compiled sprites**: pictures stored as code. They are long runs of
  `move.l/move.w/move.b #value,(a1)+` with `adda.l #skip,a1` between rows, so drawing a picture means
  running it, with no decoding.
- The rest is ordinary compiled C (around 10,000 functions), so per-stage game logic also lives in
  these modules, not only graphics.

## Hand-written assembly
34 game functions were written directly in 68000 assembly rather than C, mostly fast screen
copies (`dm`, `ddm`, `rm_lu`, `alpha_move`, `move_screen`) and the random number generator `rnd`.
That generator is a 32-bit shift register: rotate left, and when a bit falls out, flip bits 5 and 17.
