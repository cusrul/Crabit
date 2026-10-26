#pragma once

#include <Arduino.h>
#include <NanoUI.h>

class SerialCanvas : public Canvas {
public:
    SerialCanvas() : changed(true) { resetClipRect(); }
    void clear() override { for (int i = 0; i < 128 * 64; ++i) pixels[i] = 0; }
    void display() override {
        if (!changed) return;
        changed = false;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 128; ++x) Serial.print(pixels[y * 128 + x] ? '#' : ' ');
            Serial.println();
        }
        Serial.println("--- j/k focus, o toggle ---");
    }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x >= 0 && x < 128 && y >= 0 && y < 64) {
            uint8_t& pixel = pixels[y * 128 + x];
            if (color == C_INVERT) pixel = pixel ? 0 : 1;
            else pixel = color == C_BLACK ? 0 : 1;
            changed = true;
        }
    }
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
private:
    uint8_t pixels[128 * 64] = {};
    bool changed;
};

class SerialInput : public InputDriver {
public:
    void init() override {}
    UIEvent poll() override {
        if (!Serial.available()) return {EVT_NONE, 0};
        const int key = Serial.read();
        if (key == 'j' || key == 'J') return {EVT_DOWN, 0};
        if (key == 'k' || key == 'K') return {EVT_UP, 0};
        if (key == 'o' || key == 'O' || key == '\r' || key == '\n') return {EVT_OK, 0};
        return {EVT_NONE, 0};
    }
};
