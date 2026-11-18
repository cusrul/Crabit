#pragma once
// Shared hardware/NanoUI boundary for the primary-action fixtures.
//
// One game per executable: the real app.h and the real Arduboy compat adapter
// are compiled; only the Arduino/NanoUI/AmebaStore storage is faked. Including
// the real NanoUI geometry and the launcher's inherited macros first catches
// include-order, macro-leak and pointer-width regressions in each port.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define memcpy_P memcpy
#define PROGMEM

// The real Realtek/Ameba core reads only 16 bits through these macros. A port
// that routes function or data pointers through them must fail here rather
// than truncate on the watch.
#define pgm_read_byte(addr) (*((uint8_t *)(addr)))
#define pgm_read_word(addr) (*((uint16_t *)(addr)))
#define pgm_read_ptr(addr)  (*((uint16_t *)(addr)))

// Castle Boy's leaked macros, present before these ports in the real sketch TU.
#define FPS 60
#define STATE_TITLE 0
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define STATE_HELP 8

#ifdef bitToggle
#error "Arduino has no bitToggle macro; compat must expose a function, not a macro"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
uint32_t imu_updated_at = 0;
bool imu_received = false;
} // namespace AmebaStore
