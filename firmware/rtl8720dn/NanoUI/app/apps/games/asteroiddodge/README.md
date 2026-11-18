# Asteroid Dodge BW16 port

Actual source from [james-b-collins/AsteroidDodge-Arduboy](https://github.com/james-b-collins/AsteroidDodge-Arduboy),
commit `830152255d35489b601c1461defa7d1b794ee17a`, 2026-04-11.
`LICENSE` is the upstream MIT license (Copyright (c) 2026 James Collins),
unchanged (SHA-256 `b6786c9d…` raw, `41aee8f2…` LF-normalized, matching the
fixed cache). `UPSTREAM_README.md` is the original repository README. The full
468-line game state machine, fixed asteroid pattern, three-life rule, collision
boxes, score/survival timing, high-score layout and menu flow are retained.

## Play

- Tilt: move the ship left/right/up/down; the ship is clamped to the 128x64
  screen exactly as upstream.
- PAT single tap (A): select `Play`, confirm `About`/`Mute`, and continue from
  the game-over screen. `UP`/`DOWN` tilt navigates the title menu.
- PAT double tap (B): no game action in this port. The original B button was
  not used during play; B is accepted as "any button" on About/Game Over.
- Hold PAT at least 800 ms: shared adapter exit gesture.
- The original has no win state. Surviving increases the score; three hits
  (the third ends the run) produce `GAME OVER`. This is the complete original
  loss/restart loop, not an added win condition.

## Port changes and limits

- `.ino` body is included as `asteroiddodge.inc.h` inside
  `AsteroidDodgeSandbox`; only the three boundary includes
  (`Arduboy2.h`, `ArduboyTones.h`, `EEPROM.h`) were removed.
- The original `while`/`delay`-free `nextFrame()` loop is unchanged. No startup
  delay exists in the upstream source.
- `Arduboy2`/`ArduboyTones` map to the shared compatibility classes. Audio is
  silent; the original mute-menu entry and `audio.toggle()` calls remain as
  no-ops, so the menu shape is preserved.
- The Arduino EEPROM library is replaced by a game-local view over the active
  compat engine's bounded 1024-byte RAM EEPROM. The upstream layout is kept:
  high score at `0xAD`, magic byte `0xAD` at `0xAF`. It survives app re-entry
  while the engine lives, not power loss. No real EEPROM/Flash is touched.
- All upstream constant names are pushed/undefined/restored around the include
  so Castle Boy-style macros in a combined sketch cannot rewrite this source.
- `pgm_read_ptr`/`pgm_read_word` are not used for data or function pointers;
  the real Realtek macros are 16-bit. No compressed bitmap or FX path is used.

## Host verification

`tools/tests/test_arduboy30_asteroiddodge.py` compiles the real port and compat
layer with host-only Canvas/InputDriver/AmebaStore fakes and real NanoUI
geometry, Arduino bit macros (no `bitToggle`), 16-bit `pgm_read_ptr`/`pgm_read_word`
and adversarial Castle macros. It covers title navigation, real tilt movement
and screen clamps, one-second score, a real asteroid collision and life loss,
respawn invincibility, the final-hit EEPROM high score and `GAME OVER`, restart,
clean re-entry with a persistent high score, real non-empty PBM frames and the
PAT-hold exit. The injected collision positions are stated in the harness; the
loss path itself is the original `PlayerHits`/`lives`/`deathTimer` code. No
embedded build or watch hardware acceptance is claimed.

Main integration owns menu/profile and split-image wiring.
