# Pong BW16 port

Actual source from [taylorhansen/Pong-Arduboy](https://github.com/taylorhansen/Pong-Arduboy),
commit `ac2eb86d1ba9765124af7980be2d85bb608d2da6`, 2020-03-06.
`LICENSE` is the upstream MIT license (Copyright (c) 2017 Taylor Hansen),
unchanged (SHA-256 `614932e3…` raw, `cd5ccbc4…` LF-normalized). `UPSTREAM_README.md`
is the original repository README. The original
Pong v1.3.1 vector math, ball motion, paddle collision, single-player AI,
optional two-player controls, score-to-3 win screens, pause and restart states
are retained.

## Licence note: Arduboy2 Bresenham

The upstream README and `ball.cpp` state that the integer ball animation is a
version of Bresenham's line algorithm "mostly copied from Arduboy2's
implementation". Arduboy2 is BSD-3-Clause, not MIT, so the MIT repository
licence does not cover that derivation. This port keeps Taylor Hansen's
`Ball::animate()` derivation and therefore also ships the exact upstream
Arduboy2 licence text as `LICENSE-BSD-3-Arduboy2.txt`. The file is byte-for-byte
identical (verified with `cmp`) to the downloaded Arduboy2 licence cache
`arduboy2-93c33f6-LICENSE.txt`; its SHA-256 is
`5e977041c7d52c7001ace05ab0d78481cda13eb648995c311b303a05ebd73149`,
from Arduboy2 commit `93c33f6ad61eea327dd38fee9289d586eaa42a65`.

The applicable part is the BSD-3-Clause section at lines 8–50 of that file:
Arduboy2 library, Copyright (c) 2016-2017 Scott Allen, forked from the Arduboy
library and partly based on Adafruit_SSD1306, with the three BSD conditions and
disclaimer. The later sections in the same upstream licence document (BSD-2
Adafruit-GFX portions, MIT ArduboyExtra/drawCompressed, LGPL example sketches)
are not used by this port and are not claimed as part of this game.

The alternative was to replace the movement routine with the shared
`compat.drawLine`, but that routine draws a line into the framebuffer; it is not
the per-frame ball-position integrator used here. Calling it would not preserve
the original motion, so the notice-retention path was chosen instead. The MIT
file is not presented as covering the BSD-3-derived code.

## Play

- Single-player (default, the supported flow): tilt UP/DOWN moves the left
  paddle; PAT single tap (A) starts from the title; tilt LEFT pauses and resumes.
- Title: LEFT/RIGHT toggles `players: 1/2`; A starts. The original two-player
  mode is retained: the right paddle uses A (down) and B (up). Under the fixed
  PAT mapping B is a completed double tap, so two-player mode is documented as
  a secondary support range, not the primary single-player flow.
- A on either win screen restarts through the original `gameSetup()`.
- Hold PAT at least 800 ms: shared adapter exit gesture.
- The original has no "loss" screen: player 1 or player 2 reaching 3 points is
  the win condition, followed by restart.

## Port changes and limits

- The original translation units are kept as local `game.inc.h`, `paddle.inc.h`,
  `ball.inc.h`, `vector.inc.h` and their headers, included once inside
  `PongSandbox`; only the `Arduboy2.h`/`ArduboyTones.h` system includes were
  removed. `.inc.h` prevents Arduino from compiling them separately.
- The original `Serial.begin(9600)`/`Serial.write(getBuffer(), ...)` debug tap is
  removed because this firmware has no Arduino USB Serial stream.
- The original `gameSetup()` `delay(1000)` is now a frame-driven `gameWait`
  state at the same 30 fps (30 frames). No blocking wait remains.
- `arduboy.display(CLEAR_BUFFER)` keeps the original clear-after-display
  behaviour. Audio is silent compatibility.
- `abs` is resolved locally to the shared integer helper; all upstream constant
  names are pushed/undefined/restored around the include so Castle Boy-style
  macros cannot rewrite this source.
- No EEPROM, compressed bitmap, FX or external art/music path is used.
  `pgm_read_ptr`/`pgm_read_word` are not used for native pointers.

## Host verification

`tools/tests/test_arduboy30_pong.py` compiles the real port and compat layer
with host-only Canvas/InputDriver/AmebaStore fakes, real NanoUI geometry,
Arduino bit macros (no `bitToggle`), 16-bit `pgm_read_ptr`/`pgm_read_word` and
adversarial Castle macros. It covers the title toggle and single-player start,
the frame-driven setup wait, ball motion, left-paddle movement, single-player AI
reaction, wall and paddle bounce, pause/resume, three real points for each
player and both original win screens, restart, clean re-entry, real non-empty
PBM frames and the PAT-hold exit. The goal-line ball positions used to score
quickly are injected by the harness; the scoring, AI, collision and win-state
code is the original. No embedded build or watch hardware acceptance is
claimed.

Main integration owns menu/profile and split-image wiring.
