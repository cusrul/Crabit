#ifndef BW16_OLED_H
#define BW16_OLED_H

#include <SPI.h>
#include "src/NanoUI.h"
#include "AmebaStore.h"

// 硬件引脚定义
#define OLED_MOSI PA12
#define OLED_CLK  PA14
#define OLED_DC   PB1
#define OLED_RST  PB2
#define OLED_CS   PA15

class Bw16RawCanvas : public Canvas {
private:
    uint8_t buffer[1024];
    int applied_contrast = -1;
    bool applied_dim = false;

    void oled_write_byte(uint8_t dat, uint8_t cmd) {
        digitalWrite(OLED_DC, cmd ? HIGH : LOW);
        digitalWrite(OLED_CS, LOW);
        SPI.transfer(dat);
        digitalWrite(OLED_CS, HIGH);
    }

public:
    Bw16RawCanvas() { memset(buffer, 0, 1024); }

    void initHardware() {
        pinMode(OLED_DC, OUTPUT); pinMode(OLED_RST, OUTPUT); pinMode(OLED_CS, OUTPUT);
        SPI.begin();
        SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
        digitalWrite(OLED_RST, HIGH); delay(50); digitalWrite(OLED_RST, LOW); delay(100); digitalWrite(OLED_RST, HIGH); delay(100);
        uint8_t init_cmds[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
        for(uint8_t c : init_cmds) oled_write_byte(c, 0);
        applied_contrast = -1;
    }

    void clear() override { memset(buffer, 0, 1024); }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        int idx = x + (y / 8) * 128;
        int bit = (y % 8);
        if (color == C_WHITE) buffer[idx] |= (1 << bit);
        else if (color == C_INVERT) buffer[idx] ^= (1 << bit);
        else buffer[idx] &= ~(1 << bit);
    }

    void display() override {
        // The canvas owner applies received preferences only after OLED handover.
        const int contrast = AmebaStore::display_contrast;
        const bool dim = AmebaStore::display_dim;
        if (contrast >= 0 && contrast <= 255 &&
            (contrast != applied_contrast || dim != applied_dim)) {
            oled_write_byte(0x81, 0); oled_write_byte((uint8_t)contrast, 0);
            oled_write_byte(0xD9, 0); oled_write_byte(dim ? 0x11 : 0x22, 0);
            oled_write_byte(0xDB, 0); oled_write_byte(dim ? 0x00 : 0x20, 0);
            applied_contrast = contrast;
            applied_dim = dim;
        }
        for (int i = 0; i < 8; i++) {
            oled_write_byte(0xB0 + i, 0); oled_write_byte(0x00, 0); oled_write_byte(0x10, 0);
            digitalWrite(OLED_DC, HIGH); digitalWrite(OLED_CS, LOW);
            for (int n = 0; n < 128; n++) SPI.transfer(buffer[i * 128 + n]);
            digitalWrite(OLED_CS, HIGH);
        }
    }

    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
};

#endif
