#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SSD1306 panel access. Command sequences and delays are unchanged; only the
// owning file moved. hw_display and the startup task call these.
void OLED_WR_Byte(uint8_t dat, uint8_t cmd);
void OLED_Init(void);
void OLED_Fill(uint8_t data);
void OLED_SetSleep(bool sleep);

// STOP2 peripheral ownership: Display owns hspi3.

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "NanoUI/NanoUI.h"
#include <string.h>

// The only owner of hspi3, the SSD1306 panel and the BW16 display handover.
class Display {
public:
    static void init();
    static void fill(uint8_t data);
    static void writeByte(uint8_t data, uint8_t cmd);
    static void setSleep(bool sleep);
    static void setBrightness(float brightness);
    static void setInvert(bool invert);
    static void present(const uint8_t* buffer);

    static bool releaseBus();     // deinit SPI, release SCK/MOSI/RST/DC
    static void beginClaim();     // restore pins, deinit SPI for the 5 ms gap
    static bool finishClaim();    // init SPI, panel init, wake

    static bool suspend();
    static bool resume();
    static bool ready();
};

class STM32Canvas : public Canvas {
private:
    uint8_t buffer[1024];

public:
    void clear() override { memset(buffer, 0, 1024); }

    // 按颜色常量置位、清零或翻转对应像素位。
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        
        int index = x + (y / 8) * 128;
        uint8_t bit_mask = (1 << (y % 8));

        if (color == C_WHITE) {
            buffer[index] |= bit_mask;        // 置 1
        } else if (color == C_BLACK) {
            buffer[index] &= ~bit_mask;       // 清 0
        } else if (color == C_INVERT) {
            buffer[index] ^= bit_mask;        // 翻转
        }
    }

    void display() override { Display::present(buffer); }

    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
};


#endif
