#pragma once
#include <stdint.h>
#include <string.h>
#include <assert.h>
enum { C_BLACK = 0, C_WHITE = 1 };
enum { EVT_NONE = 0, EVT_OK = 1, EVT_BACK = 2 };
extern uint32_t fakeMillis;
inline uint32_t hal_get_ticks() { return fakeMillis; }
static volatile bool is_display_active = true;
inline void safe_delay(unsigned long) {}
class Canvas {
public:
    uint8_t pixels[64][128] = {};
    unsigned displays = 0;
    void clear() { memset(pixels, 0, sizeof(pixels)); }
    void drawPixel(int x, int y, int color) {
        if (x >= 0 && x < 128 && y >= 0 && y < 64) pixels[y][x] = color;
    }
    void drawLine(int,int,int,int,int) { assert(false && "legacy direct Canvas line"); }
    void drawRect(int x,int y,int w,int h,int c) {
        for (int xx=x;xx<x+w;++xx) { drawPixel(xx,y,c); drawPixel(xx,y+h-1,c); }
        for (int yy=y;yy<y+h;++yy) { drawPixel(x,yy,c); drawPixel(x+w-1,yy,c); }
    }
    void fillRect(int x,int y,int w,int h,int c) {
        for (int xx=x;xx<x+w;++xx) for (int yy=y;yy<y+h;++yy) drawPixel(xx,yy,c);
    }
    void drawText(int,int,const char*,int) { assert(false && "legacy direct Canvas text"); }
    void display() { ++displays; }
};
struct UIEvent { int type; };
using Event = UIEvent;
class InputDriver {
public:
    unsigned polls = 0;
    bool noisy = false;
    UIEvent poll() { ++polls; return {noisy ? 1 : EVT_NONE}; }
};
enum AppMode { APP_MODE_COOPERATIVE, APP_MODE_FRAME, APP_MODE_EXCLUSIVE };
class Application {
    bool exiting = false;
public:
    virtual ~Application() {}
    virtual const char* getAppName() = 0;
    virtual AppMode getMode() { return APP_MODE_COOPERATIVE; }
    virtual void onMount(Canvas*,InputDriver*) {}
    virtual void onUpdate(uint32_t) {}
    virtual void onSuspend() {}
    virtual void onResume() {}
    virtual void runExclusive(Canvas*, InputDriver*) {}
    virtual void onUnmount() {}
    void requestExit() { exiting = true; }
    bool exitRequested() const { return exiting; }
};
