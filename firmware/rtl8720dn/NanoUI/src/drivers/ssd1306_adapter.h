#ifdef ARDUINO // 只在 Arduino 下生效

#ifndef DRIVER_SSD1306_ADAPTER_H
#define DRIVER_SSD1306_ADAPTER_H

#include "../hal/canvas.h"
#include <Adafruit_SSD1306.h>

class SSD1306Adapter : public Canvas {
private:
    Adafruit_SSD1306 *drv;

public:
    SSD1306Adapter(Adafruit_SSD1306 *displayDriver) : drv(displayDriver) {}

    void clear() override { drv->clearDisplay(); }
    void display() override { drv->display(); }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        drv->drawPixel(x, y, color);
    }

    // --- 使用硬件库加速 ---
    // 虽然基类有默认实现，但 Adafruit 的肯定更快，所以我们 override 它们

    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override {
        drv->drawLine(x0, y0, x1, y1, color);
    }
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        drv->drawRect(x, y, w, h, color);
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        drv->fillRect(x, y, w, h, color);
    }
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) override {
        drv->drawRoundRect(x, y, w, h, r, color);
    }
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) override {
        drv->fillRoundRect(x, y, w, h, r, color);
    }
    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) override {
        drv->drawCircle(x, y, r, color);
    }
    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) override {
        drv->fillCircle(x, y, r, color);
    }
    void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t color) override {
        drv->drawBitmap(x, y, bitmap, w, h, color);
    }

    // --- 文本覆盖 ---
    // 继续使用 Adafruit 的文本引擎，因为它有优化
    void drawText(int16_t x, int16_t y, const char *text, uint8_t size, bool colorInvert) override {
        drv->setTextSize(size);
        if (colorInvert) drv->setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        else drv->setTextColor(SSD1306_WHITE);
        drv->setCursor(x, y);
        drv->print(text);
    }

    // 这个也要保留，利用 GFXcanvas1 做离屏渲染
    void drawTextInRect(int16_t x, int16_t y, int16_t w, int16_t h, const char *text, int16_t offset, uint8_t size) override {
        if (w <= 0 || h <= 0) return;
        GFXcanvas1 temp(w, h);
        temp.fillScreen(0);
        temp.setTextSize(size);
        temp.setTextColor(1);
        temp.setTextWrap(false);
        temp.setCursor(2 - offset, 0);
        temp.print(text);
        drv->drawBitmap(x, y, temp.getBuffer(), w, h, 1, 0);
    }

    int16_t width() override { return drv->width(); }
    int16_t height() override { return drv->height(); }
};

#endif 
#endif