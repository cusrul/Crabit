# Bananonsense BW16 port

Actual source/assets from [obono/ArduboyWorks, `bananonsense/`](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/bananonsense),
commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, version 0.02 (April 2025).
Retrieved from the shared pinned source cache. The game directory has no
separate LICENSE file; the repository root MIT license, Copyright (c) 2016–2026
OBONO, covers it and is copied unchanged as `LICENSE`.

## Play

- Directions (tilt): move the selection in menus; move the field cursor in game.
- B (watch single tap): confirm a menu item; check the six letters under the
  cursor for `BANANA`.
- A (watch double tap): open the in-game menu (continue / back to title) while
  playing; on the title and in the menu it toggles the original sound setting.
- Hold up+down (PAT hold) for the shared wrapper's exit gesture.

Find the guaranteed `BANANA` run before the 30-second timer expires. A wrong
check costs three seconds. Each find starts another wave with a different field
and one of the 48 original banana sayings; the title keeps a total across visits.

## Port changes and limits

- Original logo, title, credit, menu, field generation, letter animation,
  cursor/scroll movement, target detection, scoring, timer, game-over, retry and
  saying strings are retained; this is the upstream game, not a remake.
- `.cpp`/`.ino` bodies are staged as `.inc.h` and isolated under
  `BananonsenseSandbox` (`Logo`, `Title`, `Game` sub-namespaces). `port.h`
  pushes/hides/restores the three macros that Castle Boy actually defines
  (`FPS`, `STATE_TITLE`, `STATE_GAME_OVER`) and clears the game's own macros
  after inclusion, so ports share one sketch TU without either direction of
  macro pollution. Function-pointer tables use direct typed indexing; the
  real Realtek `pgm_read_ptr` is only 16 bits and would truncate pointers.
- `MyArduboy2` keeps the upstream 6x6 compact font, text helpers and button-edge
  state on top of the shared compatibility engine. All drawing and input still
  go through `Arduboy2Compat` / `ArduboyCompatApp`.
- The upstream `.incbin` assets are ordinary arrays. The 48 sayings (1,481 bytes
  of 5-bit text plus the 48-entry offset table) are retained and tested. The
  8,544-byte 8 kHz `banana.bin` PCM wave is **not** embedded: audio is a silent
  compatibility stub, so the original `playWave` call keeps its signature but
  has no audible effect, and no fake wave data is shipped.
- Sound on/off is the engine's silent stub; the menu icon therefore stays off
  and toggling has no audible result. The original score byte arrays are kept
  only as source assets.
- EEPROM is the engine's private 1 KB RAM emulation, not Flash. The banana total
  survives leaving and reentering the app while powered, but not reset or power
  loss. No EEPROM/Flash writes are issued. The upstream checksum reads bytes
  explicitly instead of through misaligned `uint16_t` pointers.
- Fixed undefined AVR behavior: the logo's negative/over-wide Morse shift is
  clamped (deterministic on BW16), the timer bar avoids a negative left shift,
  and the message wrapper initializes its break pointer.
- 128×64 monochrome, nominal 60 Hz, frame-driven and non-blocking to link/UI
  services. Tilt supplies only the four directions; there is no side-button
  remapping.

## Verification

`tools/tests/test_arduboy30_bananonsense.py` compiles the real `app.h` adapter,
the real `Arduboy2Compat`, and the actual game sources on the host (only NanoUI,
millis/micros and the hardware store are faked). The fixture deliberately
reproduces the Realtek 16-bit `pgm_read_ptr`/`pgm_read_word` shape and the Castle
Boy `FPS`/`STATE_*` macros, then asserts the legacy macros are restored and no
Bananonsense macro escapes; it runs the title/credit/menu/drawer paths, so a
regression back to truncated function-pointer reads fails at compile or runtime.
It covers logo/title render,
credit, menu selection, start countdown, tilt movement, wrong-check time
penalty, a real B-button find, saying decoding (indices 0 and 47), the in-game
menu/record write, continue, back-to-title, timeout/game-over/retry, exit and
reentry resets, RAM-record persistence, the PAT-hold exit gesture and non-empty
framebuffers. Inputs are deterministic: 17 ms fake frames, tilt pulses and
single/double PAT taps; no wall clock or network is used. Set
`CRABIT_ARDUBOY30_BANANONSENSE_FRAMES=<dir>` to write the actual PBMs.

This is host evidence only. No BW16 embedded build, Flash/FTL fit check, device
run, timing measurement or hardware acceptance is claimed here; those remain
main-AI integration checks.
