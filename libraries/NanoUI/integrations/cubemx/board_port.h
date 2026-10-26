#pragma once
#include <NanoUI.h>
// Supply these in your user-owned board_port.cpp; SDK includes live there.
Canvas& nanoui_canvas();
InputDriver& nanoui_input();
uint32_t nanoui_ticks();
void nanoui_delay(uint32_t milliseconds);
