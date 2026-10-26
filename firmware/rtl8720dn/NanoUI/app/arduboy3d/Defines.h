#pragma once

#if _WIN32
#include <stdint.h>
#include <string.h>
#define PROGMEM
#define PSTR
#define pgm_read_byte(x) (*((uint8_t*)x))
#define pgm_read_word(x) (*((uint16_t*)x))
#define pgm_read_ptr(x) (*((uintptr_t*)x))
#define strlen_P(x) strlen(x)
#else
// 针对 BW16 (ARM Cortex-M33) 的安全宏
// 彻底抛弃 <avr/pgmspace.h> 的危险定义
#include <stdint.h>
#include <string.h>

#ifndef PROGMEM
  #define PROGMEM
#endif
#ifndef PSTR
  #define PSTR
#endif

// 强制按字节读取：完美解决 ARM 架构下的非对齐访问 (Unaligned Access) 死机问题！
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*((const uint8_t *)(addr)))
#endif

// 将 16 位读取拆分为两次安全的 8 位读取，再组合
#ifndef pgm_read_word
#define pgm_read_word(addr) ( (uint16_t)(*((const uint8_t *)(addr))) | ((uint16_t)(*((const uint8_t *)(addr) + 1)) << 8) )
#endif

// 将 32 位/指针读取拆分为四次安全的 8 位读取，再组合
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr)  ( (uintptr_t)(*((const uint8_t *)(addr))) | ((uintptr_t)(*((const uint8_t *)(addr) + 1)) << 8) | ((uintptr_t)(*((const uint8_t *)(addr) + 2)) << 16) | ((uintptr_t)(*((const uint8_t *)(addr) + 3)) << 24) )
#endif

#ifndef strlen_P
#define strlen_P(x) strlen(x)
#endif
#endif

#ifdef _WIN32
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#else
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#endif

#define TARGET_FRAMERATE 30
#define DEV_MODE 0

#define INPUT_LEFT 1
#define INPUT_RIGHT 2
#define INPUT_UP 4
#define INPUT_DOWN 8
#define INPUT_A 16
#define INPUT_B 32

#define COLOUR_WHITE 1
#define COLOUR_BLACK 0

#define FIXED_ANGLE_MAX 256

#define CAMERA_SCALE 1
#define CLIP_PLANE 32
#define CLIP_ANGLE 32
#define NEAR_PLANE_MULTIPLIER 130
#define NEAR_PLANE (DISPLAY_WIDTH * NEAR_PLANE_MULTIPLIER / 256)
#define HORIZON (DISPLAY_HEIGHT / 2)

#define CELL_SIZE 256
#define PARTICLES_PER_SYSTEM 8
#define BASE_SPRITE_SIZE 16
#define MAX_SPRITE_SIZE (DISPLAY_HEIGHT / 2)
#define MIN_TEXTURE_DISTANCE 4
#define MAX_QUEUED_DRAWABLES 12

#define TURN_SPEED 3
