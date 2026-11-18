# Evasion — BW16 source port

Original game and assets: [OBONO / ArduboyWorks, evasion](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/evasion),
pinned commit `d4b1f041789dcd1d71907654e4025d613b4ab420`.
Copyright (c) 2016-2026 OBONO, MIT; `LICENSE` is copied verbatim from that
commit's repository root (there is no per-game licence file).

## Controls

- Tilt left/right moves the runner; no sustained A/B input is required.
- B (watch single tap) confirms menu items and restarts after game over; A
  (watch double tap) pauses during play and opens the title/game menus,
  matching upstream.
- Upstream simple mode is preserved: hold tilt down to open settings.
- PAT hold >= 800 ms exits. RGB LED hints are silent no-ops on this device.

## Port changes

`port.h` composes the upstream units as `.inc.h` under `EvasionSandbox`, with
logo/title/game in separate namespaces. `EvasionApp` is frame-driven on
`ArduboyCompatApp`; setup/loop keep upstream mode transitions.

- Module, title handler/drawer tables use typed function-pointer arrays. AVR
  `pgm_read_word` dispatch was removed because Realtek `pgm_read_ptr` reads only
  sixteen pointer bits.
- Playtune is not compiled; `MyArduboy2` keeps the upstream audio API as silent
  stubs plus a RAM-only sound flag. No PCM/score data is embedded.
- EEPROM helpers use the engine's bounded 1 KiB RAM EEPROM with byte-wise
  checksum reads. No Flash persistence.
- Shared 5x7 text/clipping replaces the local 6x6 drawing helper; game physics,
  collision, scoring, menus, title art, dithering and all sprites are retained.
  Two lines use explicit colour/`uintptr_t` arguments to keep double and pointer
  arithmetic portable; the dither parity, not the address width, is what matters.
- Every mutable game/title/menu/EEPROM cursor is reset on mount. No blocking
  waits, tasks or SDK timers were added.
- CastleBoy's leaked `STATE_*` macros are harmless here; the port still avoids
  reusing those names outside its sandbox.

Host evidence only: `tools/tests/test_arduboy30_evasion.py` compiles the real
sources with real NanoUI geometry, Arduino bit macros and the 16-bit
`pgm_read_ptr` shape, then checks title, movement/scoring, pause/continue, exit,
PBM frames and re-entry reset. Embedded/RAM/hardware acceptance is main-AI/user
work.
