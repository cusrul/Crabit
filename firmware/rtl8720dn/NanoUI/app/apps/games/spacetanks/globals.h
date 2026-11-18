#pragma once

#include "MyArduboy.h"

#define FRAMERATE 60
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

struct EngineState {
    Arduboy arduboy;
};
static EngineState *g_engine = nullptr;
#define arduboy (g_engine->arduboy)
