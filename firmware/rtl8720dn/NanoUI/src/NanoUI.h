#ifndef NANO_UI_H
#define NANO_UI_H

// 在其他头文件之前先处理宏名冲突。
#include "NanoUI_Preflight.h"

// 引入 Arduino 后取消其数学宏，使标准库声明可用。
#ifdef ARDUINO
    #include <Arduino.h>
    // Arduino.h 定义了同名宏，会干扰 std 实现。
    #undef abs
    #undef min
    #undef max
    #undef round
#endif

// 核心逻辑 (PC 和 Arduino 通用)
#include "core/window_manager.h"
#include "hal/canvas.h"
#include "hal/input.h"
#include "hal/hal_time.h"
// 组件 (PC 和 Arduino 通用)
#include "widgets/flex.h"
#include "widgets/text.h"
#include "widgets/basic.h"
#include "widgets/container.h"
#include "widgets/button.h"
#include "widgets/carousel.h"
#include "widgets/image.h"
#include "widgets/controls.h"
#include "widgets/popup.h"
#include "widgets/transition.h"
#include "widgets/animated_image.h"
#include "widgets/modern_menu.h"
#include "widgets/scroll.h"
#include "framework/app.h"
#include "framework/app_manager.h"
#include "core/sensor_manager.h"

#endif
