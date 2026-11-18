# Memory Rhythm BW16 port

Actual source from [tuxinator2009/Arduboy_MemoryRhythm](https://github.com/tuxinator2009/Arduboy_MemoryRhythm),
commit `3cb80b5ce0ec6167522e8f3271eccff351a727b1`, 2020-07-31.
`LICENSE` is the upstream MIT license (the upstream file was `LICENSE.txt`),
byte-for-byte unchanged (SHA-256 `b31fd3ca…` raw, `43dd6c63…` LF-normalized).
`UPSTREAM_README.md` is the original repository README, including its simulator
controls. `graphics.h` is the original
repo-owned sprite/road/icon data. The 190-line xorshift Simon-style sequence,
LEARN/PLAY/DONE states, score, octave and speed progression are retained.

## Play

- The game starts on the original DONE/start screen (Score 0, Speed 1). PAT
  single tap (A) or double tap (B) starts a three-move LEARN sequence.
- During LEARN the six actions are shown one at a time: UP = jump, DOWN = duck,
  LEFT/RIGHT = slide, A = raise left, B = raise right. Each has its own figure
  pose and its own icon; the host test verifies all six rendered poses are
  pairwise different.
- During PLAY, tilt the same four directions or tap A/B in the displayed order.
  A correct action advances; completing the round increases the sequence length
  (3 → 4 → …) and eventually the speed/octave. A wrong action ends the round.
- The original has no win state: the DONE screen is the loss/end screen, and
  A/B restarts at score 0 with a three-move sequence.
- Hold PAT at least 800 ms: shared adapter exit gesture.

## Silence and double-tap

Audio is a silent compatibility stub. The original sequence is still fully
playable without sound because the LEARN sequence is encoded in the sprite and
icon frames, not only in pitch; the host test captures the real LEARN frames and
verifies the six possible poses are distinct. The original PLAY state waits for
input indefinitely (there is no per-move deadline), so the PAT double-tap
latency for B cannot make the game unwinnable. B is delivered as the adapter's
completed double-tap pulse and is exercised through the real action path.

## Port changes and limits

- `.ino` body is included as `memoryrhythm.inc.h` inside
  `MemoryRhythmSandbox`; the three boundary includes were removed, the original
  `#include "graphics.h"` and all assets are retained.
- The original `jumpOffsets[actionDuration]` read index 16 on the first frame of
  a jump, one past the 16-element array. The port clamps that index to 15; the
  intended jump animation and all source rules are otherwise unchanged.
- `Arduboy2` maps to a local subclass that adds the upstream `println()` text
  helpers; `ArduboyTones` adds the upstream one-argument continuous `tone()`;
  `Sprites` uses the shared sprite renderer. Audio remains silent.
- `TONE_HIGH_VOLUME`/`CLEAR_BUFFER` are game-local aliases, and every constant
  name used as an identifier is pushed/undefined/restored around the include so
  Castle Boy-style macros cannot rewrite this source.
- No EEPROM, compressed bitmap, FX or external asset path is used.
  `pgm_read_ptr`/`pgm_read_word` are not used for native pointers.

## Host verification

`tools/tests/test_arduboy30_memoryrhythm.py` compiles the real port and compat
layer with host-only Canvas/InputDriver/AmebaStore fakes, real NanoUI geometry,
Arduino bit macros (no `bitToggle`), 16-bit `pgm_read_ptr`/`pgm_read_word` and
adversarial Castle macros. It covers the DONE start, distinct silent action
poses, a real three-move LEARN sequence, replay through UP/DOWN/LEFT/RIGHT/A/B,
correct advance to a four-move round, the original wrong-action DONE path, A
restart, clean re-entry, real non-empty PBM frames and the PAT-hold exit. No
embedded build or watch hardware acceptance is claimed.

Main integration owns menu/profile and split-image wiring.
