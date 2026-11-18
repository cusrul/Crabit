// Compile-only TU used for nm evidence: all 21 catalogue ports in registry
// order, with no Application instantiated. If any port still kept its engine or
// game objects in static storage, a large .bss/.data symbol would appear.
#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define memcpy_P memcpy
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))

#include "app/apps/games/ardubullets/app.h"
#include "app/apps/games/bananonsense/app.h"
#include "app/apps/games/chiemagari/app.h"
#include "app/apps/games/chribocchi/app.h"
#include "app/apps/games/evasion/app.h"
#include "app/apps/games/knightmove/app.h"
#include "app/apps/games/ardugo/app.h"
#include "app/apps/games/minesweeper/app.h"
#include "app/apps/games/game2048/app.h"
#include "app/apps/games/galaxion/app.h"
#include "app/apps/games/spacetanks/app.h"
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

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
uint32_t imu_updated_at = 0;
bool imu_received = false;
}
