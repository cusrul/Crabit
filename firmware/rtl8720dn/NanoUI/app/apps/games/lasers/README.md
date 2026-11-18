# Lasers — BW16 source port

Original game and assets: [OBONO / ArduboyWorks, lasers](https://github.com/obono/ArduboyWorks/tree/d4b1f041789dcd1d71907654e4025d613b4ab420/lasers),
pinned commit `d4b1f041789dcd1d71907654e4025d613b4ab420`.
Copyright (c) 2016-2026 OBONO, MIT; see `LICENSE` (copied from that commit's root).
Downloaded through GitHub REST and raw URLs, not reconstructed gameplay.

## Controls and warning

- Directions move; B (watch single tap) switches player polarity during play.
- A (watch double tap) pauses once lasers arrive; A returns to the title after
  game over.
- Original menu controls: Up/Down choose, B (single tap) confirms; A's original
  sound toggle is silent in this port. B accepts the flashing warning and can
  retry after game over.
- The original alternating gray framebuffer **flickers rapidly**. The original
  seizure/discomfort warning is retained; do not play if sensitive to flashing.

## Port changes

`lasers_app.h` provides `LasersSandbox` and the frame-driven `LasersApp` on the
shared `ArduboyPortApp`/`ArduboyPort`. All upstream `.cpp`/`.ino` files are `.inc.h`
for Arduino header staging without independent recursive compilation. Original game physics,
polarity/laser collision, scoring, bitmap/font assets, title, warning and menus remain.

- Typed module calls replace AVR 16-bit pointer truncation and byte offsets.
- Portable framebuffer loops replace AVR assembly; logo bit shifts are range-checked.
- Local helper retains OBONO's original compact font and clipped drawing, without
  AVR boot/flashlight waits, timers, RGB hardware, Playtune allocation or speaker calls.
- EEPROM helpers use the engine's bounded byte-addressed RAM only. Record checksum
  avoids type-punning. No Flash persistence or actual audio is provided.
- The upstream game-over score's invalid byte duration `0, 260` is encoded as
  `1, 4` (260 ms, high byte then low byte), eliminating C++ list narrowing even
  though audio playback remains disabled. Numeric text formatting explicitly
  dispatches its temporary buffer to the `const char*` overload.
- Setup resets every mutable game/helper/menu state and RAM record on reentry;
  no blocking delays or waits. Shared base owns frame timing, input and canvas lifecycle.
- Title translation-unit state is renamed locally; macros are cleaned up at the
  wrapper boundary to avoid collisions with other ports.

### Integer/random portability audit

The only `rand()` call mixes a title-screen seed with `micros()`; there is no
`RAND_MAX` division or 15-bit random scaling to reproduce. Spawn positions,
velocities, slopes and spark variation use bounded Arduino `random(...)` calls.
Plain `char` handles ASCII text only; signed physics fields already use `int8_t`
or `int16_t`. Positions, slopes and angles remain within those fields' ranges.
Unsigned score/frame wrapping is retained from upstream.

The signed 16-bit cumulative laser counter could overflow during long sessions
and produce a negative laser-array index. It is now `uint32_t`, and the spawn
interval reduction is bounded before subtraction to prevent unsigned underflow.
Coordinate downscaling now explicitly floors signed division rather than relying
on implementation-defined negative right shifts, retaining AVR rounding.
All game-defined macros, including temporary `STATE_T`, `STATE_INIT` and `state`
aliases, are undefined after inclusion; the wrapper header guard is intentional.

This is a source adaptation, not a hardware acceptance claim. Native/combined
embedded verification is owned by the integrating session; no device was operated.
