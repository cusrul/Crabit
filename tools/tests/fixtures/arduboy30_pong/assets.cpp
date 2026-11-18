// Compile-only companion to the runtime harness: the port's own translation
// unit with no test driver. It proves the displayed menu text is emitted as
// namespace-scoped read-only objects that the game code references, which is
// what lets the split linker attribute `.rodata.<namespace>*` to this port.
#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))

#include "app/apps/games/pong/app.h"

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
}

// The production registry installs a live State before running a port. Without
// that, the compiler sees a null sandbox pointer here and folds every drawing
// body into a trap, which would hide the text references this TU is meant to
// expose. Externally visible so the pointer escape cannot be optimised away.
static PongSandbox::State pongState;
__attribute__((used)) void crabitArduboyPortInstallState()
{
    PongSandbox::installPortState(&pongState);
}
