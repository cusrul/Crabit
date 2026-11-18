

#pragma once

#include <stdint.h>
#if _WIN32
#include <math.h>
#endif
#include "Defines.h"

#define FIXED_SHIFT 8
#define FIXED_ONE (1 << FIXED_SHIFT)

// 修改点 1：显式类型转换
// 在 32 位机器上，确保宏展开时参与运算的类型是清晰的。
// 游戏原始逻辑依赖 16 位整数的行为，我们强制转换为 int16_t，防止 32 位下的隐式提升导致意外。
#define INT_TO_FIXED(x) ((int16_t)((x) * FIXED_ONE))
#define FIXED_TO_INT(x) ((int16_t)((x) >> 8))
#define FLOAT_TO_FIXED(x) ((int16_t)((x) * FIXED_ONE))

#define ABS(x) (((x) < 0) ? -(x) : (x))

#define FIXED_ANGLE_MAX 256
#define FIXED_ANGLE_WRAP(x) ((x) & 255)
#define FIXED_ANGLE_45 (FIXED_ANGLE_90 / 2)
#define FIXED_ANGLE_90 (FIXED_ANGLE_MAX / 4)
#define FIXED_ANGLE_180 (FIXED_ANGLE_90 * 2)
#define FIXED_ANGLE_270 (FIXED_ANGLE_90 * 3)
#define FIXED_ANGLE_TO_RADIANS(x) ((x) * (2.0f * 3.141592654f / FIXED_ANGLE_MAX))

// 修改点 2：移除 PROGMEM 声明
extern const int16_t sinTable[FIXED_ANGLE_MAX];

inline int16_t FixedSin(uint8_t angle)
{
    // 修改点 3：替换 pgm_read_word 为直接数组访问
    return sinTable[angle];
}

inline int16_t FixedCos(uint8_t angle)
{
    // 替换 pgm_read_word
    return sinTable[FIXED_ANGLE_WRAP(FIXED_ANGLE_90 - angle)];
}

uint16_t Random();
void SeedRandom(uint16_t seed);