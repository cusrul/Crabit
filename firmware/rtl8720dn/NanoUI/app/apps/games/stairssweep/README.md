# Stairssweep BW16 port

Actual source/assets from [obono/ArduboyWorks](https://github.com/obono/ArduboyWorks),
commit `d4b1f041789dcd1d71907654e4025d613b4ab420`, path `stairssweep`
(OBN-Y09, v0.10, October 2019). The MIT root licence is unchanged; `LICENSE`
is the upstream text, SHA-256 `62a5db2ff173434f94d055a9432f5655267e92f4db92a042b832884e8a48f6bb`.

This is the complete original falling-block puzzle: title/record/credit menus,
six level choices, six-column stair field, three-box bunches, rotation, fast
fall, boxes/enemies, the ball and its stuck/bounce rules, star gauge and star
cancellation, horizontal line erase, bonus scoring, game over and the original
level/score records. It is not a template or a remake.

## Play

- Tilt supplies the original `ROTATE_DPAD` mapping (no `FLIP_SCREEN`), so the
  watch directions are the original rotated game directions. The physical
  orientation was not hardware-tested.
- Move the falling bunch horizontally with the original LEFT/RIGHT axis.
- Rotate: original logical UP + B. On this watch the original UP direction is
  one tilt axis; a PAT single-B while holding it rotates. The added alias also
  accepts tilt-up + single-B, so the physical `tilt-up + single-B` combination
  the watch can express is host-verified to rotate. While B is held, the
  rotate-axis lateral step is suppressed so the tap does not also nudge the
  bunch.
- Star cancellation uses the same held UP+B combination (the original
  `buttonPressed` path), now including the tilt-up alias.
- Fast fall uses the original logical DOWN press/hold path.
- PAT double-A pauses; B chooses CONTINUE/RESTART/QUIT. A also toggles sound
  in menus, matching the original.
- PAT hold is the shared adapter exit gesture.

Do not read this as support for simultaneous opposite tilts: UP+DOWN (and
LEFT+RIGHT) are not tested or claimed. Only the single-axis directions and the
documented UP+B / tilt-up+B combination are exercised.

## Port changes and limits

- Original `common.h`/`data.h` and `common.cpp`/`game.cpp`/`title.cpp`/
  `logo_v.cpp`/`menu_v.cpp` bodies are kept private under `StairssweepSandbox`.
  `port.h` hides and restores the Castle Boy `FPS`/`STATE_*` macros and the
  compatibility `ALIGN_*` names around the original enums.
- The Realtek `pgm_read_ptr`/`pgm_read_word` are 16 bits. The original function
  pointer tables are indexed directly as typed function-pointer arrays; no AVR
  pointer truncation is used.
- The original `MyArduboyV` 6x6 font, text metrics and score/tone call sites
  remain, but audio is silent through the shared compatibility stub.
- Records use the private RAM EEPROM (`arduboy.eeprom`), not AVR `EEPROM` and
  never Flash. Base 736, signature + record + byte-safe checksum, checked to
  stay inside the 1024-byte engine buffer. The checksum is computed byte-wise
  because the 32-bit target has no 16-bit aliasing guarantee.
- Fixed an audited upstream typo in the ball-decay path: `ballE == 0;` became
  `ballE = 0;` (the original comparison had no effect, so a stuck ball never
  cleared through that branch).
- The logo Morse shift is clamped non-negative (the original could shift by a
  negative count).
- Mutable state (mode, record cursor, menu cursor, field, bunch, ball, star,
  score and counters) resets on app entry and unmount; the RAM EEPROM record
  survives visits, so records can be re-read.
- 128x64 monochrome, nominal 60 Hz, frame-driven. No blocking loops, compressed
  bitmaps, AVR asm, timers or SDK services.

## Verification

`tools/tests/test_arduboy30_stairssweep.py` compiles the real `app.h` adapter,
the real `Arduboy2Compat`, and the actual port sources on the host with
Realtek's 16-bit `pgm_read_ptr`/`pgm_read_word` shape and the Castle Boy
macros. It asserts those macros are restored and Stairs Sweep macros do not
leak, then covers the real title, menu start, tilt movement, `UP+B` rotation,
star cancellation, an injected legal full-line erase and score change, field
boundary checks, unmount/reentry reset and the PAT exit gesture, plus non-empty
framebuffers. The injected full line is the only synthetic board state; it uses
the original erase/score rule. Deterministic 17 ms fake frames and taps are
used. Set `CRABIT_ARDUBOY30_STAIRSSWEEP_FRAMES=<dir>` for PBMs.

Host evidence only; no BW16 embedded build, Flash/FTL fit, device run or
hardware acceptance is claimed.
