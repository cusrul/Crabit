#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ===================== 硬件引脚定义 =====================
// 适用于 Realtek RTL8720DN / BW16
#define BTN_UP_PIN    PA27 
#define BTN_DOWN_PIN  PA15 // 注意：与 OLED_CS 共用
#define BTN_OK_PIN    PA30 
#define BTN_BACK_PIN  PA7  

// OLED SPI 引脚
#define OLED_MOSI   PA14 
#define OLED_CLK    PA12 
#define OLED_DC     PB1  
#define OLED_RST    PB2  
#define OLED_CS     PA15 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#endif