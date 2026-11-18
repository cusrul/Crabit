# Hollow Seeker — BW16 port

Original game by OBONO, MIT, copyright (c) 2016–2026 OBONO. See `LICENSE`.
Source: https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/hollow
Fetched using GitHub REST/raw at that exact commit; not a remake. Original cave
generation, movement, collision, animations, title, records, credits and bitmap/font
assets are retained. `info.json` is unchanged upstream metadata.

## Controls

- Raw UP/DOWN: choose title menu item.
- Raw LEFT/RIGHT: walk through the cave; shelter in a hollow before it closes.
- Raw A or B: select, leave records/credits, or retry after game over.
- Hold raw UP+DOWN: common NanoUI adapter's exit gesture.
- No tilt controls or device ownership changes in this port.

## Changes and limits

- `hollow_app.h` includes sources once in `HollowSeekerSandbox` and exposes
  `HollowSeekerApp` through `ArduboyCompatApp` (`APP_MODE_FRAME`). C++/INO bodies
  renamed `.inc.h` so Arduino stages them as headers without recursively compiling
  them outside the namespace.
- `MyArduboy` derives from the shared `Arduboy2Compat`, retaining upstream 6×6 font,
  pixel layout, clipping helpers and button edge semantics. Frame pacing, lifecycle,
  Canvas and raw input translation belong to the shared adapter.
- AVR library startup/flashlight infinite loop, timer and speaker hardware calls
  removed. Audio and RGB effects are muted; SOUND remains OFF. Score bytes remain
  as upstream data except the malformed 260ms byte pair is corrected to 1,4.
- AVR EEPROM pointer casts/busy waits replaced by shared per-engine RAM EEPROM.
  Records survive game rounds and App re-entry while the engine remains alive,
  but never write real EEPROM/Flash and are lost on firmware restart.
- String tables use full-width typed pointers; module dispatch was already typed.
  Explicit signed 8-bit movement/hollow counters and 16-bit score preserve AVR
  semantics. Game-local 15-bit RNG avoids ARM/host RAND_MAX changing generation.
- Logo's out-of-range/negative Morse shifts are bounded. All mutable upstream
  state and local input/font/RNG state are reset on setup; records reload from RAM.
  Transitions stay frame-driven, without delays or busy waits.

Hardware gameplay, OLED timing and split-Flash execution require user validation.
No firmware build or hardware acceptance is implied by this source port.
