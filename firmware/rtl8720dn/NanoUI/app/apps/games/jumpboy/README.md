# JumpBoy BW16 port

Actual source/assets from [t-iwasaki/arduboy-JumpBoy](https://github.com/t-iwasaki/arduboy-JumpBoy),
commit `1b2dcc9c1e9929f8e02e372998c80e287f1aa3c3`, root MIT
("Copyright 2020 t-iwasaki"). `LICENSE` is the upstream text unchanged;
SHA-256 `984396ec8147737c9ef84e814ec6816cb17f4edcb760eb7fa1856b34477dece4`
(LF-normalised; raw checkout with CRLF is
`b63f275ebaff769c30061930f5003212b12130ba4dcb5759036e073a1faf008e`).

This is the complete original 2016 small arcade game: title and blinking
prompt, three lives, bouncing ball, scrolling spring floor, coins, heart/
spring/coin/flag power-ups, twenty-coin stages, progressive speed-up, stage
clear and game over. It is not a template or a remake.

## Play

- Tilt left/right moves the ball horizontally through the original
  `LEFT_BUTTON`/`RIGHT_BUTTON` cases. Tilt up/down still writes the original
  `pDir` value; like upstream, only the horizontal cases change `pX`.
- A or B (PAT tap/double tap) starts from the title; the original
  "Press A or B" blink and two-second `bStart` screen are kept.
- Hold PAT (shared adapter gesture) exits.
- Silent build: every original `sound.tone` call site remains, but the local
  `ArduboyTones` is the compatibility stub.

## Port changes and limits

- Original `JumpBoy.ino`, `Coins.ino`, `PowerUps.ino` and `Spring.ino` bodies
  stay private under `JumpBoySandbox` as `jumpboy.inc.h`, `coins.inc.h`,
  `powerups.inc.h` and `spring.inc.h`. `Bitmaps.h` is unchanged except for a
  unique include guard. `port.h` saves/restores `FPS`, the `MODE_*` names and
  the `NOTE_*` names that collide with the compatibility layer.
- The title `while (true) { delay(30); ... }`, `levelStart`'s `delay(2000)`,
  the stage-clear `delay(2000)`, the miss `delay(3000)` and the game-over
  `delay(3000)` became frame states/counters driven by the adapter's 60 Hz
  `nextFrame()`. The original logic still advances at 30 Hz (every second
  adapter frame); no blocking wait, `millis()` pacing or busy loop remains.
- `Arduboy2::drawSlowXYBitmap` is supplied by a local `JumpBoyArduboy`
  subclass using the upstream pixel body and `pgm_read_byte`. `drawBitmap`
  uses the shared compatibility implementation.
- The original `pDir == 90;` no-effect statement is retained as written.
- JumpBoy has no save. `record` (best stage reached) is session RAM only and
  every transient — mode, player, springs, coins, power-up, timers and record
  — is reset on app entry and unmount. No Flash, EEPROM or SDK storage is used.
- 128x64 monochrome, nominal 60 Hz, frame-driven; no heap, compressed bitmaps,
  AVR asm, timers or shared services.

## Verification

`tools/tests/test_arduboy30_jumpboy.py` compiles the real `app.h` adapter, the
real `Arduboy2Compat` and the actual port sources on the host with Realtek's
16-bit `pgm_read_ptr`/`pgm_read_word` shape and preceding Castle Boy/`FPS`/
`MODE_TITLE`/`NOTE_*` macros. It asserts the restored macros and that no
JumpBoy mode macro leaks, then covers the real title, A start and two-second
start screen, tilt movement, a real spring bounce with spring depletion, real
coin collision/score, the real twenty-coin stage-clear transition, a miss with
its three-second hold, the last-life game-over return to the title, the RAM-only
record scope across unmount/reentry, the PAT exit gesture and non-empty
framebuffers/PBMs.

The miss branch uses one injected legal board state (a single surviving spring
outside the landing window); all other transitions are driven through the
original handlers and input. Deterministic 17 ms fake frames are used. Set
`CRABIT_ARDUBOY30_JUMPBOY_FRAMES=<dir>` for PBMs.

Host evidence only; no BW16 embedded build, Flash/FTL fit, device run or
hardware acceptance is claimed.
