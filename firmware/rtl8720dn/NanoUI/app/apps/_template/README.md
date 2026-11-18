# Example App

Replace this file. An App package needs only `app.h`, `README.md` and `LICENSE`
(plus optional `src/`/`assets/`); there is no JSON manifest and no per-App
Context. Record:

- what the app does and how to open it;
- controls and exit gesture;
- build profile/flags and the single registration in
  `app/arduboy30_registry.h` (`#include`, `CRABIT_ARDUBOY30_ENABLE_<ID>` and the
  `entryFor<Class>("id", "menu label")` entry);
- for an Arduboy port only: `mode` (`frame`), upstream pin and `requires`
  dependencies such as `arduboy2-compat`;
- hardware/radio permissions;
- what was tested on a host and what still needs user-operated hardware testing;
- upstream source, pinned commit and license (SPDX id must match `LICENSE`).

This package intentionally has no `app.json`; menu metadata lives in C++ once.
