# Space Tanks BW16 port

Actual source/assets from [joho/spacetanks](https://github.com/joho/spacetanks),
commit `c4882b967aa10061f23ccc457261143f112b3e74`, root MIT (John Barton &
David Goodlad, 2016). `LICENSE` is the upstream MIT text unchanged. Art is by
@dgoodlad (ASE/BMP sources included upstream). The README's Star Flight-derived
starfield and function-pointer state machine come from TEAMarg's
`ID-26-Star-Flight`, which is MIT (Team-ARG-Museum archive); that attribution is
retained here.

This is the real 2016 game: banner, six-line intro, tank movement, laser
cooldown, bat/Dracula/Mega-Dracula waves, collisions, score, death message and
banner restart. It is not a template or a new remake.

## Play

- Directions (tilt): move the tank one pixel per frame, with the original field
  limits. The original cursor-position writes are retained (they do not print
  text).
- A or B (PAT tap/double tap): start from the banner, skip the intro after its
  one-second gate, and fire the laser. Fire has the original 30-frame cooldown;
  no button hold is required.
- Auto fire is ON by default and only acts in the real playing state: the
  banner, intro and death message never fire. On the banner, up/down toggles
  `AUTO:ON`/`AUTO:OFF` (the current value is drawn beside the banner art) and
  the choice lasts for the powered session. With it off, the manual A/B tap is
  the only fire path.
- Hold up+down (PAT hold): shared adapter exit gesture.
- Death is the original hit animation followed by a three-second flashing
  "YOU LOSER" message and a return to the banner.

## Port changes and limits

- Original `globals.h`/`game.h`/`state.h`/`intro.h`/`utils.h`/`death.h` bodies
  are isolated under `SpaceTanksSandbox`; `port.h` hides and restores the Castle
  Boy macros that precede this port in the combined sketch.
- The original `pgm_read_word(mainGameLoop + state)` dispatch is replaced by
  direct typed table indexing. The Realtek `pgm_read_ptr` is only 16 bits and
  would truncate the function pointer on the 32-bit target.
- Fixed two audited host-startup defects while retaining behaviour: the bat
  path step now wraps at `>= pathLength` (the original `> pathLength` could index
  one past the 4-step path), and the 360-frame spawn period uses
  `frameCount % spawnRate` instead of a truncating `everyXFrames(uint8_t)`
  call (which became 104 frames). The redundant in-state `display()` was
  removed; the frame adapter displays once per frame.
- Audio is silent through the shared compatibility stub. The unconfirmed
  music score is not imported: `intro.h` has no `music.h` dependency and the
  intro's `playScore` call is removed. Tone/score call sites elsewhere are
  accepted by the local silent `tunes` member; no audio data is shipped.
- Mutable state (state machine, bat array, player, score, cooldown, hit pointer,
  flashing message and star positions) resets on app entry and unmount. No
  EEPROM or Flash writes; there is no persistence.
- Auto fire is a port addition: `handleInput()` treats the laser as pressed
  when the session preference is ON (it already runs only in
  `GAME_STATE_PLAYING`), and `stateBanner()` owns the up/down edge toggle plus
  the small label drawn over the banner art. The preference is the only value
  that survives re-entry; it is RAM-only and lost on power-off.
- 128x64 monochrome, nominal 60 Hz, frame-driven; banner/intro/death are
  frame-counted states, not blocking loops. No compressed bitmaps, AVR asm,
  timers or SDK services are used.
- Tilt supplies the four directions; no side-button remapping.

## Verification

`tools/tests/test_arduboy30_spacetanks.py` compiles the real `app.h` adapter,
real `Arduboy2Compat`, and actual game sources on the host with Realtek's
16-bit `pgm_read_ptr`/`pgm_read_word` shape and the Castle Boy `FPS`/`STATE_*`
macros. It asserts the legacy macros are restored and Space Tanks macros do not
leak. It covers banner/start, intro skip, tilt movement, a real B-button laser
hit and score, the path-index boundary, a real bounding-box collision, death
message, banner reset, unmount/reentry reset and the PAT exit gesture, plus
non-empty framebuffers. Deterministic 17 ms fake frames and taps are used; no
wall clock or network. Set `CRABIT_ARDUBOY30_SPACETANKS_FRAMES=<dir>` for PBMs.

This is host evidence only; no BW16 embedded build, Flash/FTL fit, device run or
hardware acceptance is claimed.
