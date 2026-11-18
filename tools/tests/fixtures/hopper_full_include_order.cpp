// Compile-only staged-order guard: the real launcher includes Castle Boy,
// Arduboy3D, Hopper, Hollow, SameGame and Lasers, then the 21 catalogue ports.
// Hopper must not leave or absorb private macros in either direction.
#include <stdint.h>
#include <string.h>
#define memcpy_P memcpy
#define strcpy_P strcpy

#include "app/apps/games/castle_boy/app.h"
#include "app/apps/games/arduboy3d/app.h"
#include "app/apps/games/hopper/app.h"
#include "app/apps/games/hollow/app.h"
#include "app/apps/games/samegame/app.h"
#include "app/apps/games/lasers/app.h"
#include "app/apps/games/ardubullets/app.h"
#include "app/apps/games/bananonsense/app.h"
#include "app/apps/games/chiemagari/app.h"
#include "app/apps/games/chribocchi/app.h"
#include "app/apps/games/evasion/app.h"
#include "app/apps/games/knightmove/app.h"
#include "app/apps/games/ardugo/app.h"
#include "app/apps/games/minesweeper/app.h"
#include "app/apps/games/game2048/app.h"
#include "app/apps/games/spacetanks/app.h"
#include "app/apps/games/galaxion/app.h"
#include "app/apps/games/stairssweep/app.h"
#include "app/apps/games/toyokumono/app.h"
#include "app/apps/games/rysk/app.h"
#include "app/apps/games/reversi/app.h"
#include "app/apps/games/quarto/app.h"
#include "app/apps/games/asteroiddodge/app.h"
#include "app/apps/games/memoryrhythm/app.h"
#include "app/apps/games/pong/app.h"
#include "app/apps/games/jumpboy/app.h"
#include "app/apps/games/longcat/app.h"

// The six old Apps still compile beside Hopper's full port.
int main() {
    AppHopper *hopper = nullptr;
    (void)hopper;
    return 0;
}

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
uint32_t imu_updated_at = 0;
bool imu_received = false;
}
