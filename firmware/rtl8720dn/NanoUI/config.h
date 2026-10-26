#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Temporary fixed-stage diagnostics for the ATTACH hardware investigation.
#ifndef CRABIT_BW16_DIAG_ATTACH_TRACE
#define CRABIT_BW16_DIAG_ATTACH_TRACE 1
#endif

// Temporary ATTACH bisection: exercise admission/reply without RAM pairing.
#ifndef CRABIT_BW16_DIAG_ATTACH_ADMISSION_ONLY
#define CRABIT_BW16_DIAG_ATTACH_ADMISSION_ONLY 1
#endif

// Product size control: normal static-icon menu, no large animation/lab payloads.
#ifndef CRABIT_BW16_DIAG_SMALL_APP_SET
#define CRABIT_BW16_DIAG_SMALL_APP_SET 1
#endif

// ===================== 硬件引脚定义 =====================
// 适用于 Realtek RTL8720DN / BW16
#define BTN_UP_PIN    PA27 
#define BTN_DOWN_PIN  PA15 // 注意：与 OLED_CS 共用
#define BTN_OK_PIN    PA30 
#define BTN_BACK_PIN  PA7  

// OLED SPI 引脚
#define OLED_MOSI   PA12 
#define OLED_CLK    PA14 
#define OLED_DC     PB1  
#define OLED_RST    PB2  
#define OLED_CS     PA15 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#endif
