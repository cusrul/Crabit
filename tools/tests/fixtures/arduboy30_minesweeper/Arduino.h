#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
class __FlashStringHelper;
#define F(text) reinterpret_cast<const __FlashStringHelper*>(text)
typedef uint8_t byte;
extern uint32_t fakeMillis;
inline unsigned long millis() { return fakeMillis; }
inline unsigned long micros() { return uint32_t(fakeMillis * 1000U + 17); }
inline void randomSeed(unsigned long seed) { srand(seed); }
inline long random(long upper) { return upper > 0 ? rand() % upper : 0; }
inline long random(long lower, long upper) { return upper > lower ? lower + random(upper-lower) : lower; }
#define DEG_TO_RAD 0.017453292519943295769236907684886
inline char* itoa(int value, char* output, int) { sprintf(output, "%d", value); return output; }
// A forbidden dependency should fail the link, not silently become a no-op.
void delay(unsigned long);
