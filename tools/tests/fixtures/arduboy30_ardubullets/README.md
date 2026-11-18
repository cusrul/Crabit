# Combined host fixture

`old_ports_then_new.cpp` mirrors the staged production include order: real
NanoUI geometry, then the Arduino bit macros (the real core does not define
`bitToggle`), then the six existing games, then the 21 completed ports. `-I`
points at a copied sketch root, so the fixture still sees the real `compat/`
headers, game sources and the host `Arduino.h`/`AmebaStore.h`/`NanoUI.h` shims.
Only those completed packages are copied; in-progress games are not mixed in.

Two shape details are deliberate:

- `pgm_read_ptr`/`pgm_read_word` read only 16 bits, matching the Realtek SDK
  where native pointers are 32-bit. A typed table must not rely on that
  truncation.
- `memcpy_P` maps to `memcpy`, matching the Realtek SDK's Arduino header. This
  is an accurate simulation of an SDK-provided helper, not a claim that the
  fixture is a self-contained Arduino core.

The test copies the sources and then gives every staged `.h`/`.hpp` header the
same mtime. That reproduces a clean-checkout/copy condition where GCC 14 and ARM
GCC 6.5 treat byte-identical `#pragma once` helpers as the same file; the Evasion
helper was skipped and the Evasion type disappeared. Port helpers therefore use
unique `#ifndef` guards as well as unique comment text. Include-only `.inc.h`
bodies have no `#pragma once`, so the guard check applies only to guarded headers.
