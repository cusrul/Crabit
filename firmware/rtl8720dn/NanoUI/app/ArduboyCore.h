#ifndef ARDUBOY_CORE_H
#define ARDUBOY_CORE_H

#include "../src/NanoUI.h"
#include <Arduino.h>

#ifndef PROGMEM
  #define PROGMEM
#endif
#ifndef pgm_read_byte
  #define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif

#undef WHITE
#undef BLACK
#define WHITE 1
#define BLACK 0
#define WIDTH 128
#define HEIGHT 64
#define RGB_ON 0    
#define RGB_OFF 1

#define LEFT_BUTTON  0x20
#define RIGHT_BUTTON 0x40
#define UP_BUTTON    0x08
#define DOWN_BUTTON  0x04
#define A_BUTTON     0x02
#define B_BUTTON     0x01

#define NOTE_C1 0
#define NOTE_C2 0
#define NOTE_CS2 0
#define NOTE_D2 0
#define NOTE_E2 0
#define NOTE_F2 0
#define NOTE_G1 0
#define NOTE_G2 0
#define NOTE_G3 0
#define NOTE_G4 0
#define NOTE_GS2 0
#define NOTE_GS3 0
#define NOTE_GS4 0
#define NOTE_GS5 0
#define NOTE_A2 0
#define NOTE_B2 0
#define NOTE_C3 0
#define NOTE_CS3 0
#define NOTE_CS3H 0
#define NOTE_D3 0
#define NOTE_E3 0
#define NOTE_F3 0
#define NOTE_A3 0
#define NOTE_B3 0
#define NOTE_C4 0
#define NOTE_CS4 0
#define NOTE_D4 0
#define NOTE_E4 0
#define NOTE_F4 0
#define NOTE_A4 0
#define NOTE_B4 0
#define NOTE_C5 0
#define NOTE_CS5 0
#define NOTE_D5 0
#define NOTE_E5 0
#define NOTE_F5 0
#define NOTE_G5 0
#define NOTE_A5 0
#define NOTE_B5 0
#define NOTE_C6 0
#define NOTE_CS6 0
#define NOTE_CS7 0
#define NOTE_E6 0
#define TONES_REPEAT 0

#define Arduboy2Base Arduboy2
#define ALIGN_LEFT   0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT  2

class ArduboyTones {
public:
    ArduboyTones() {}
    template<typename T> ArduboyTones(T arg) {} 
    void tone(uint32_t f, uint32_t d) {}
    void tone(uint32_t f1, uint32_t d1, uint32_t f2, uint32_t d2) {}
    void tone(uint32_t f1, uint32_t d1, uint32_t f2, uint32_t d2, uint32_t f3, uint32_t d3) {}
    void tones(const uint16_t *t) {}
};

class Arduboy2;
static Arduboy2* GlobalArduboyEngine = nullptr;

class Arduboy2 {
private:
    Canvas* _canvas = nullptr; 
    uint8_t hwBtn = 0, curBtn = 0, prevBtn = 0; 
    uint32_t frameCount = 0;
    uint32_t lastPollFrame = 0xFFFFFFFF; // 防连击自锁器
    int16_t cursorX = 0, cursorY = 0;

public:
    uint8_t sBuffer[1024]; 

    struct { void read(int a, void* b, int c){ memset(b,0,c); } void write(int a, const void* b, int c){} uint8_t read(int a){return 0;} void update(int a, uint8_t v){} } eeprom;
    struct AudioStub {
        static bool enabled() { return false; } 
        void off(){} void on(){} void saveOnOff(){}
    } audio;

    Arduboy2() { memset(sBuffer, 0, 1024); GlobalArduboyEngine = this; }

    void attachCanvas(Canvas* c) { _canvas = c; }

    uint8_t* getBuffer() { return sBuffer; } 
    void digitalWriteRGB(uint8_t r, uint8_t g, uint8_t b) {} 
    void boot() {} void flashlight() {} void systemButtons() {} void bootLogo() {}
    int cpuLoad() { return 0; } uint16_t generateRandomSeed() { return 0; }
    void fillScreen(uint8_t color) { fillRect(0, 0, WIDTH, HEIGHT, color); }

    // 按键引擎：防止同一帧内重复轮询
    void updateHwButton(uint8_t s) { hwBtn = s; }
    void pollButtons() { 
        if (lastPollFrame == frameCount) return; // 一帧之内，绝不刷新第二次！
        prevBtn = curBtn; 
        curBtn = hwBtn; 
        lastPollFrame = frameCount;
    }
    bool pressed(uint8_t b) { return (curBtn & b); }
    bool notPressed(uint8_t b) { return !(curBtn & b); }
    bool justPressed(uint8_t b) { return (curBtn & b) && !(prevBtn & b); }

    void begin() {}
    void setFrameRate(uint8_t r) {}
    
    // 帧率兜底：未调用 pollButtons() 的游戏也会刷新按键
    bool nextFrame() { 
        frameCount++; 
        pollButtons(); 
        return true; 
    }
    bool nextFrameDEV() { return nextFrame(); }
    bool everyXFrames(uint8_t x) { return (frameCount % x) == 0; }

    void clear() { memset(sBuffer, 0, 1024); }
    
    void drawPixel(int16_t x, int16_t y, uint8_t color) {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
        uint16_t idx = (y / 8) * WIDTH + x;
        if (color) sBuffer[idx] |= (1 << (y % 8));
        else       sBuffer[idx] &= ~(1 << (y % 8));
    }
    void fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t c) { 
        for(int i = 0; i < w; i++) for(int j = 0; j < h; j++) drawPixel(x+i, y+j, c); 
    }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c) { 
        if(_canvas) _canvas->drawLine(x0, y0, x1, y1, c ? 1 : 0); 
    }
    void drawRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t c) { 
        if(_canvas) _canvas->drawRect(x, y, w, h, c ? 1 : 0); 
    }
    void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t w, uint8_t h) { drawBitmap(x, y, bitmap, w, h, WHITE); }
    void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t w, uint8_t h, uint8_t color) {
        for (int j = 0; j < h; j += 8) {
            for (int i = 0; i < w; i++) {
                uint8_t b = pgm_read_byte(bitmap++);
                for(int bit = 0; bit < 8; bit++) {
                    if (j+bit >= h) break;
                    if (b & (1<<bit)) drawPixel(x+i, y+j+bit, color);
                }
            }
        }
    }

    void setCursor(int16_t x, int16_t y) { cursorX = x; cursorY = y; }
    void print(const char* s) { if(_canvas) _canvas->drawText(cursorX, cursorY, s, 1); }
    void print(int v) { char buf[12]; itoa(v, buf, 10); print(buf); }

    void display() {
        if (!_canvas) return;
        _canvas->clear();
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                if (sBuffer[(y / 8) * WIDTH + x] & (1 << (y % 8))) _canvas->drawPixel(x, y, 1);
            }
        }
        _canvas->display(); 
    }
    void display(bool clearOS) { display(); if (clearOS) clear(); }
};

class Sprites {
private:
    Arduboy2* _ab;
public:
    Sprites() { _ab = GlobalArduboyEngine; } 
    Sprites(Arduboy2 &ab) : _ab(&ab) {}

    void drawOverwrite(int16_t x, int16_t y, const uint8_t *img, uint8_t f) {
        if(!_ab) return;
        uint8_t w = pgm_read_byte(img);
        uint8_t h = pgm_read_byte(img+1);
        int bpf = w * ((h + 7) / 8);
        const uint8_t *data = img + 2 + (f * bpf);
        for(int j = 0; j < h; j += 8) {
            for(int i = 0; i < w; i++) {
                uint8_t b = pgm_read_byte(data++);
                for(int bit = 0; bit < 8; bit++) {
                    if (j+bit >= h) break;
                    _ab->drawPixel(x+i, y+j+bit, (b & (1<<bit)) ? WHITE : BLACK);
                }
            }
        }
    }
    void drawPlusMask(int16_t x, int16_t y, const uint8_t *img, uint8_t f) {
        if(!_ab) return;
        uint8_t w = pgm_read_byte(img);
        uint8_t h = pgm_read_byte(img+1);
        int bpf = w * ((h + 7) / 8) * 2; 
        const uint8_t *data = img + 2 + (f * bpf);
        for(int j = 0; j < h; j += 8) {
            for(int i = 0; i < w; i++) {
                uint8_t imgByte = pgm_read_byte(data++); 
                uint8_t maskByte = pgm_read_byte(data++); 
                for(int bit = 0; bit < 8; bit++) {
                    if (j+bit >= h) break;
                    if (maskByte & (1<<bit)) _ab->drawPixel(x+i, y+j+bit, (imgByte & (1<<bit)) ? WHITE : BLACK);
                }
            }
        }
    }
    void drawSelfMasked(int16_t x, int16_t y, const uint8_t *img, uint8_t f) {
        if(!_ab) return;
        uint8_t w = pgm_read_byte(img);
        uint8_t h = pgm_read_byte(img+1);
        int bpf = w * ((h + 7) / 8);
        const uint8_t *data = img + 2 + (f * bpf);
        for(int j = 0; j < h; j += 8) {
            for(int i = 0; i < w; i++) {
                uint8_t b = pgm_read_byte(data++);
                for(int bit = 0; bit < 8; bit++) {
                    if (j+bit >= h) break;
                    if (b & (1<<bit)) _ab->drawPixel(x+i, y+j+bit, WHITE);
                }
            }
        }
    }
    void drawExternalMask(int16_t x, int16_t y, const uint8_t *img, const uint8_t *mask, uint8_t f, uint8_t mask_f) {
        if(!_ab) return;
        uint8_t w = pgm_read_byte(img);
        uint8_t h = pgm_read_byte(img+1);
        int bpf = w * ((h + 7) / 8);
        const uint8_t *data = img + 2 + (f * bpf);
        const uint8_t *mdata = mask + 2 + (mask_f * bpf);
        for(int j = 0; j < h; j += 8) {
            for(int i = 0; i < w; i++) {
                uint8_t imgByte = pgm_read_byte(data++);
                uint8_t maskByte = pgm_read_byte(mdata++);
                for(int bit = 0; bit < 8; bit++) {
                    if (j+bit >= h) break;
                    if (maskByte & (1<<bit)) _ab->drawPixel(x+i, y+j+bit, (imgByte & (1<<bit)) ? WHITE : BLACK);
                }
            }
        }
    }
};
#endif