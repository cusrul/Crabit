#ifndef CRABIT_ARDUBOY30_SELECTED_H
#define CRABIT_ARDUBOY30_SELECTED_H

// Default sketch selection: no additional Arduboy port is compiled or listed.
// tools/build_bw16_split_games.py --arduboy30 rewrites this file only inside an
// isolated staged build with CRABIT_ARDUBOY30_ENABLE_<ID> macros. Menu labels,
// classes and factories live in app/arduboy30_registry.h; ordinary six-game
// images stay on the existing launcher profile path.
#define CRABIT_ARDUBOY30_COUNT 0

#endif
