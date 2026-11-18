# ARDUBULLET — BW16 source port

Original game and assets: [OBONO / ArduboyWorks, ardubullets](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/ardubullets),
pinned commit `d4b1f041789dcd1d71907654e4025d613b4ab420`.
Copyright (c) 2016-2026 OBONO, MIT; see `LICENSE` (copied from that commit's root).
The upstream directory carries no separate per-game licence, so the repository
root MIT notice is the governing licence for code and bundled bitmaps.

## Controls

- Tilt moves the ship. A single PAT tap is B, the upstream fire and
  menu-confirm key: it triggers one short burst (four shots at the current
  rate); the movement-slow flag rises only for that brief pulse, so it is a
  short nudge rather than sustained slow fire.
- Auto fire is ON by default and acts only in the real battle state. After the
  start animation each burst refills as soon as the previous one finishes, so
  the original shot rate and pattern stay unchanged and B is never held (ship
  speed is not slowed). The pause menu's `AUTO FIRE ON/OFF` row switches it off
  for the powered session; with it off the manual single-tap bursts above are
  the only fire path. Title, pause and game-over screens never fire.
- A double PAT tap is A: it opens the pause menu during play and toggles the
  (silent) sound row in menus, matching upstream. Up/Down selects and the
  single tap confirms, matching upstream B.
- PAT hold >= 800 ms exits the port. The original RGB LED blink setting only
  changes a stored flag; this device has no RGB LED to drive.

## Port changes

`port.h` composes the upstream units as `.inc.h` bodies under
`ArdubulletsSandbox`, with each former translation unit in its own namespace so
private `static` names cannot collide with other ports. The frame-driven
`ArdubulletsApp` uses `ArduboyCompatApp`; upstream `setup`/`loop` remain, driven
by real `nextFrame()` pacing.

- The AVR module table stored 16-bit function pointers and fetched them with
  `pgm_read_word`; it is now a typed table, with identical logo/title/game
  transitions.
- The Playtune AVR timer implementation is not included. `MyArduboy2` keeps the
  upstream audio API as silent stubs and a RAM-only sound flag, so scores and
  the menu indicator follow real state without pretending audio exists.
- `common.cpp` EEPROM helpers now use the engine's bounded 1 KiB byte-addressed
  RAM EEPROM. Checksums read bytes explicitly (no unaligned type punning).
  Records never reach Flash and are intentionally lost on power-off.
- Local 6x6 drawing helpers are replaced by the shared 5x7 font/clipping; the
  upstream game, enums, physics, bullet patterns, scoring, menus, title and all
  bitmaps are unchanged.
- The shared adapter exposes a per-port PAT action policy. This port maps the
  single tap to B and the double tap to A because B is the upstream
  fire/confirm key; the tap resolution and its 220 ms window stay in STM32, so
  the only change is which Arduboy button each resolved tap stands for. The
  pause menu gains one `AUTO FIRE ON/OFF` row (default ON, session-scoped); the
  fire site refills `playerFire` only while `STATE_PLAYING`, so menus, the
  start animation and the game-over sequence keep their upstream paths.
- The upstream starfield read pseudo-random bytes from AVR instruction code.
  That is not portable, so it uses an explicit 24-byte table; gameplay is
  unaffected.
- Arduboy collision geometry is `CrabitArduboy::Point/Rect`, aliased to the
  upstream names only inside `ArdubulletsSandbox`; it never redeclares NanoUI's
  global `Point`/`Rect`. The record's bit toggle uses
  `CrabitArduboy::toggleBit`, not Arduino's missing `bitToggle` macro.
- The two function-local shot/bullet cursors became namespace state so
  exit/re-entry can reset them; every mutable game, title, menu and logo field
  is reset on mount. No blocking waits, timers or tasks were added.

These are source adaptations verified natively, not a hardware acceptance
claim. The main-AI session owns combined embedded linkage and any device trial.
