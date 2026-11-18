#ifndef CRABIT_ARDUBOY30_REGISTRY_H
#define CRABIT_ARDUBOY30_REGISTRY_H

// Single typed C++ registration for every Arduboy game in an Arduboy-ports
// image: 27 entries over five gameplay categories.
//
// This table is the only menu factory source. The catalogue in docs/nanoui/
// arduboy30-catalog.json stays an audit/selection record, not runtime metadata:
// it names 21 opt-in ports, while the six built-in games are part of every
// ports image and are never selectable. Both kinds are rows in the same table
// with the same factory, so the menu never splits "old" from "new" games.
// A package carries app.h + README.md + LICENSE; its class, label and category
// live here once, so packages keep no app.json.
//
// Selection: tools/build_bw16_split_games.py --arduboy30 rewrites only the
// staged copy of app/arduboy30_selected.h to define CRABIT_ARDUBOY30_ENABLE_<ID>
// for the selected catalogue ids. Selectable rows compile and list only when
// their macro is defined; builtInEntryFor rows are always present.
//
// Package metadata that used to live in app.json:
// - entry is always app.h; mode is frame for every Arduboy port;
// - requires is arduboy2-compat (the shared Arduboy compat layer);
// - version is the upstream pin recorded in each README/catalogue;
// - license is the upstream SPDX id (MIT) and each package keeps its LICENSE.
#include <stddef.h>
#include <stdint.h>
#include "../src/NanoUI.h"
#include "core/app_factory.h"

// Shooter (4)
#if defined(CRABIT_ARDUBOY30_ENABLE_ARDUBULLETS)
#include "apps/games/ardubullets/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_GALAXION)
#include "apps/games/galaxion/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_SPACETANKS)
#include "apps/games/spacetanks/app.h"
#endif
#include "apps/games/arduboy3d/app.h"

// Jump/Action (10)
#include "apps/games/castle_boy/app.h"
#if defined(CRABIT_ARDUBOY30_ENABLE_JUMPBOY)
#include "apps/games/jumpboy/app.h"
#endif
#include "apps/games/hopper/app.h"
#include "apps/games/hollow/app.h"
#include "apps/games/lasers/app.h"
#if defined(CRABIT_ARDUBOY30_ENABLE_EVASION)
#include "apps/games/evasion/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_CHRIBOCCHI)
#include "apps/games/chribocchi/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_RYSK)
#include "apps/games/rysk/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_TOYOKUMONO)
#include "apps/games/toyokumono/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_ASTEROIDDODGE)
#include "apps/games/asteroiddodge/app.h"
#endif

// Puzzle (8)
#include "apps/games/samegame/app.h"
#if defined(CRABIT_ARDUBOY30_ENABLE_BANANONSENSE)
#include "apps/games/bananonsense/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_CHIEMAGARI)
#include "apps/games/chiemagari/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_MINESWEEPER)
#include "apps/games/minesweeper/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_GAME2048)
#include "apps/games/game2048/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_STAIRSSWEEP)
#include "apps/games/stairssweep/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_LONGCAT)
#include "apps/games/longcat/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_KNIGHTMOVE)
#include "apps/games/knightmove/app.h"
#endif

// Board (3)
#if defined(CRABIT_ARDUBOY30_ENABLE_ARDUGO)
#include "apps/games/ardugo/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_REVERSI)
#include "apps/games/reversi/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_QUARTO)
#include "apps/games/quarto/app.h"
#endif

// Casual/Rhythm (2)
#if defined(CRABIT_ARDUBOY30_ENABLE_PONG)
#include "apps/games/pong/app.h"
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_MEMORYRHYTHM)
#include "apps/games/memoryrhythm/app.h"
#endif

namespace Arduboy30Registry {

// Gameplay order is also menu order: the category folder list follows it.
enum class Category : uint8_t {
    Shooter = 0,
    JumpAction,
    Puzzle,
    Board,
    CasualRhythm,
    Count,
};

struct Entry {
    const char* id;
    const char* label;
    Category category;
    VoidCallback launch;
};

inline const char* categoryLabel(Category category) {
    switch (category) {
        case Category::Shooter: return "射击";
        case Category::JumpAction: return "动作跳跃";
        case Category::Puzzle: return "益智解谜";
        case Category::Board: return "棋类对弈";
        case Category::CasualRhythm: return "休闲节奏";
        default: return "";
    }
}

// Registered rows carry a plain function pointer; NanoUI's VoidCallback is
// void (*)(), not std::function, so capturing lambdas cannot be pushed here.
// Both helpers create the typed callback: prepare resources first (managed
// ports through CrabitArduboy::tryCreate<T>, plain apps failable), then launch
// only on success. Failure leaves the current App and UI unchanged.
template <typename T>
inline void launchOne() {
    Application* app = CrabitArduboy::createForMenu<T>();
    if (app) AppManager::launch(app);
}

// Selectable catalogue port: compiled and listed only when the staged
// selection enables it.
template <typename T>
constexpr Entry entryFor(const char* id, const char* label, Category category) {
    return Entry{id, label, category, &launchOne<T>};
}

// Built-in game: part of every Arduboy-ports image, so it has no selection
// macro. Same row shape and factory as a selectable port.
template <typename T>
constexpr Entry builtInEntryFor(const char* id, const char* label, Category category) {
    return Entry{id, label, category, &launchOne<T>};
}

// Rows are grouped by category, top to bottom, in the gameplay order above.
inline const Entry* entries(size_t& count) {
    static const Entry table[] = {
        // Shooter
#if defined(CRABIT_ARDUBOY30_ENABLE_ARDUBULLETS)
        entryFor<ArdubulletsApp>("ardubullets", "ARDUBULLET", Category::Shooter),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_GALAXION)
        entryFor<GalaxionApp>("galaxion", "Galaxion", Category::Shooter),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_SPACETANKS)
        entryFor<SpaceTanksApp>("spacetanks", "Space Tanks", Category::Shooter),
#endif
        builtInEntryFor<Arduboy3DApp>("arduboy3d", "Arduboy 3D", Category::Shooter),

        // Jump/Action
        builtInEntryFor<CastleBoyApp>("castle_boy", "Castle Boy", Category::JumpAction),
#if defined(CRABIT_ARDUBOY30_ENABLE_JUMPBOY)
        entryFor<JumpBoyApp>("jumpboy", "JumpBoy", Category::JumpAction),
#endif
        builtInEntryFor<AppHopper>("hopper", "Hopper", Category::JumpAction),
        builtInEntryFor<HollowSeekerApp>("hollow", "Hollow Seeker", Category::JumpAction),
        builtInEntryFor<LasersApp>("lasers", "Lasers", Category::JumpAction),
#if defined(CRABIT_ARDUBOY30_ENABLE_EVASION)
        entryFor<EvasionApp>("evasion", "Evasion", Category::JumpAction),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_CHRIBOCCHI)
        entryFor<ChribocchiApp>("chribocchi", "Chri-Bocchi Cat", Category::JumpAction),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_RYSK)
        entryFor<RyskApp>("rysk", "RYSK", Category::JumpAction),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_TOYOKUMONO)
        entryFor<ToyokumonoApp>("toyokumono", "Toyokumono", Category::JumpAction),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_ASTEROIDDODGE)
        entryFor<AsteroidDodgeApp>("asteroiddodge", "Asteroid Dodge", Category::JumpAction),
#endif

        // Puzzle
        builtInEntryFor<SameGameApp>("samegame", "SameGame", Category::Puzzle),
#if defined(CRABIT_ARDUBOY30_ENABLE_BANANONSENSE)
        entryFor<BananonsenseApp>("bananonsense", "Bananonsense", Category::Puzzle),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_CHIEMAGARI)
        entryFor<ChiemagariApp>("chiemagari", "Chie Magari Ita", Category::Puzzle),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_MINESWEEPER)
        entryFor<MinesweeperApp>("minesweeper", "Minesweeper", Category::Puzzle),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_GAME2048)
        entryFor<Game2048App>("game2048", "2048", Category::Puzzle),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_STAIRSSWEEP)
        entryFor<StairssweepApp>("stairssweep", "Stairssweep", Category::Puzzle),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_LONGCAT)
        entryFor<LongCatApp>("longcat", "LongCat", Category::Puzzle),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_KNIGHTMOVE)
        entryFor<KnightMoveApp>("knightmove", "Knight Move", Category::Puzzle),
#endif

        // Board
#if defined(CRABIT_ARDUBOY30_ENABLE_ARDUGO)
        entryFor<ArduGoApp>("ardugo", "ArduGo", Category::Board),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_REVERSI)
        entryFor<ReversiApp>("reversi", "Reversi", Category::Board),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_QUARTO)
        entryFor<QuartoApp>("quarto", "Quarto!", Category::Board),
#endif

        // Casual/Rhythm
#if defined(CRABIT_ARDUBOY30_ENABLE_PONG)
        entryFor<PongApp>("pong", "Pong", Category::CasualRhythm),
#endif
#if defined(CRABIT_ARDUBOY30_ENABLE_MEMORYRHYTHM)
        entryFor<MemoryRhythmApp>("memoryrhythm", "Memory Rhythm", Category::CasualRhythm),
#endif

        // Sentinel: the C++ standard rejects zero-size arrays, and an image
        // always has at least the six built-in rows.
        {nullptr, nullptr, Category::Count, nullptr}
    };
    count = sizeof(table) / sizeof(table[0]) - 1;
    return table;
}

// Count of compiled rows in one category; 0 means the category folder must not
// be shown. The menu filters with this instead of hardcoding profile lists.
inline size_t countIn(Category category) {
    size_t count = 0;
    const Entry* rows = entries(count);
    size_t found = 0;
    for (size_t i = 0; i < count; i++) {
        if (rows[i].category == category) found++;
    }
    return found;
}

}  // namespace Arduboy30Registry

#endif
