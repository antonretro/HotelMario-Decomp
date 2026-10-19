# Hotel Mario - trivia and cut content

Found while decompiling. Retail = Hotel Mario (Europe), program built 1994-04-06.
Prototype = USA beta, 1993-11-23. Addresses are offsets in `cdi_hotel`.

## Holiday messages on the loading screen
`loading_screen` reads the player's clock (`_sysdate`). Next to the line about each Koopaling
"preparing for your arrival", `load_stage` (0x8fc8-0x90c7) holds date-based messages:
New Year, Valentine's Day, Mother's Day, "schools almost out", Father's Day, Fourth of July,
back to school, a Halloween "BOOOOO...", Thanksgiving ("GOBBLE GOBBLE") and Happy Holidays.
There's also a birthday message, "ITS HOLLIES BIRTHDAY", for someone named Hollie, probably
connected to the development team. **None of this is in the 1993 prototype.** Still to do:
decompile `load_stage` to get the exact dates.

## A censored function name
The original symbol table has a function called `get_the_fxxxxxx_bonus` (0x7bc). The
programmers censored the swear word themselves in the source.

## Easter eggs ("eegg")
Retail adds an easter-egg system the prototype doesn't have: global `eegg`,
`room_eegg_init`, `eegg_coll_det` (collision with the egg) and `sfb_eegg_det`.
There's also `luigi_hat_spit` (0x23ce), which is retail-only too. Still to do: find out what triggers them.

## There is no hotel 6
Stage files go `L0, L1, L2, L3, L4, L5, L7, L8`, with no `L6`, on both the retail disc and the
1993 prototype. Either a hotel was cut before November 1993, or the numbering skipped 6 on purpose.

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

## Hand-written assembly
34 game functions were written directly in 68000 assembly rather than C, mostly fast screen
copies (`dm`, `ddm`, `rm_lu`, `alpha_move`, `move_screen`) and the random number generator `rnd`.
That generator is a 32-bit shift register: rotate left, and when a bit falls out, flip bits 5 and 17.
