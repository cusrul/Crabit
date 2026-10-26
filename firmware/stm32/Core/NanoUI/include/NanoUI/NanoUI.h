#ifndef NANO_UI_H
#define NANO_UI_H

// 核心逻辑 (PC 和 Arduino 通用)
#include <NanoUI/core/window_manager.h>
#include <NanoUI/core/embedded_vector.h>
#include <NanoUI/core/text.h>
#include <NanoUI/core/callback.h>
#include <NanoUI/hal/canvas.h>
#include <NanoUI/hal/input.h>
#include <NanoUI/hal/hal_time.h>
// 组件 (PC 和 Arduino 通用)
#include <NanoUI/widgets/flex.h>
#include <NanoUI/widgets/text.h>
#include <NanoUI/widgets/basic.h>
#include <NanoUI/widgets/container.h>
#include <NanoUI/widgets/button.h>
#include <NanoUI/widgets/carousel.h>
#include <NanoUI/widgets/image.h>
#include <NanoUI/widgets/controls.h>
#include <NanoUI/widgets/popup.h>
#include <NanoUI/widgets/transition.h>
#include <NanoUI/widgets/animated_image.h>
#include <NanoUI/widgets/modern_menu.h>
#include <NanoUI/framework/app.h>
#include <NanoUI/framework/app_manager.h>
#include <NanoUI/core/sensor_manager.h>
// --- 硬件相关驱动 (仅在 Arduino 下编译) ---
#ifdef ARDUINO
    #include <Arduino.h>
    #include <NanoUI/drivers/ssd1306_adapter.h>
    #include <NanoUI/drivers/button_driver.h>
#endif

#endif
