#ifndef MAIN_ENTRY_H
#define MAIN_ENTRY_H

void main_setup();
void main_loop();

// 向 launcher.h 及子 App 导出以下变量。
#include "../AmebaTypes.h"
extern volatile BW16Mode current_mode;

#endif
