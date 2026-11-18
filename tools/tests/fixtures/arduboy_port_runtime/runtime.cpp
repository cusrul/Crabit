#include "app/compat/arduboy/ArduboyCompatApp.h"
#include <limits.h>
#include <iostream>
#include <type_traits>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
}

static bool pixel(const Arduboy2Compat& port, int x, int y) {
    return (port.sBuffer[y/8*128+x] & (1U << (y&7))) != 0;
}

static void primitives() {
    static_assert(!std::is_reference<decltype(ArduboyPortMath::min(1,2))>::value,
                  "math helpers must return values, not references to local arguments");
    assert(ArduboyPortMath::min(int8_t(-4), 2) == -4);
    assert(ArduboyPortMath::max(3, int16_t(-5)) == 3);
    assert(ArduboyPortMath::abs(int8_t(-128)) == 128);
    assert(ArduboyPortMath::abs(-3.5f) == 3.5f);
    Arduboy2Compat p;
    Canvas canvas;
    p.attachCanvas(&canvas);
    p.clear();
    p.drawLine(-3,-3,4,4,WHITE);
    for (int i=0;i<=4;++i) assert(pixel(p,i,i));
    p.drawLine(0,0,4,4,BLACK);
    for (int i=0;i<=4;++i) assert(!pixel(p,i,i));
    p.drawRect(1,1,4,4,INVERT);
    assert(pixel(p,1,1) && pixel(p,4,4) && !pixel(p,2,2));
    p.drawRect(1,1,4,4,INVERT);
    for (uint8_t value:p.sBuffer) assert(value == 0);
    p.drawRect(0,0,1,1,INVERT); assert(pixel(p,0,0));
    p.drawRect(0,0,1,1,INVERT); assert(!pixel(p,0,0));
    p.fillRect(-2,-2,4,4,WHITE);
    assert(pixel(p,0,0) && pixel(p,1,1) && !pixel(p,2,2));
    p.fillRect2(0,0,4,-1,BLACK); assert(pixel(p,0,0));
    p.fillScreen(BLACK);
    p.drawCircle(10,10,3,INVERT);
    assert(pixel(p,13,10) && pixel(p,10,7) && !pixel(p,10,10));
    p.drawCircle(10,10,3,INVERT);
    for (uint8_t value:p.sBuffer) assert(value == 0);
    p.fillCircle(10,10,2,WHITE);
    assert(pixel(p,10,10) && pixel(p,12,10) && !pixel(p,12,12));
    p.fillCircle(10,10,2,BLACK); assert(!pixel(p,10,10));
    const uint8_t image[] = {0xFF,0xFF};
    p.drawBitmap(127,63,image,2,8,INVERT);
    assert(pixel(p,127,63));
    p.drawBitmap(127,63,image,2,8,INVERT); assert(!pixel(p,127,63));
    p.drawFastHLine2(0,0,128,WHITE);
    p.drawFastVLine2(0,0,64,WHITE);
    p.display(); assert(canvas.pixels[0][127] && canvas.pixels[63][0]);
    p.clear();
    p.setCursor(0,0); assert(p.print(F("AB 12")) == 5);
    p.display();
    bool visible=false;
    for (int y=0;y<6;++y) for(int x=0;x<30;++x) visible |= canvas.pixels[y][x];
    assert(visible && p.cursor_x == 30);
    p.setCursor(0,6); p.print(LONG_MIN);
    p.fillScreen(WHITE); p.fillScreen(INVERT);
    for (uint8_t value:p.sBuffer) assert(value == 0);
}

static void spritesAndShapes() {
    Arduboy2Compat p;
    Canvas canvas;
    p.attachCanvas(&canvas);
    p.clear();
    // Two frames, each 2x8 packed as one byte per column.
    const uint8_t overwrite[] = {2, 8, 0xFF, 0x00, 0x00, 0xFF};
    SpritesCompat sprites(p);
    sprites.drawOverwrite(0, 0, overwrite, 0);
    assert(pixel(p, 0, 0) && !pixel(p, 1, 0));
    sprites.drawSelfMasked(4, 0, overwrite, 1);
    assert(!pixel(p, 4, 0) && pixel(p, 5, 0));
    sprites.drawErase(8, 0, overwrite, 0);
    assert(!pixel(p, 8, 0));
    const uint8_t plusMask[] = {1, 8, 0xFF, 0xFF};
    sprites.drawPlusMask(12, 0, plusMask, 0);
    assert(pixel(p, 12, 0));
    const uint8_t external[] = {1, 8, 0x00};
    const uint8_t mask[] = {1, 8, 0xFF};
    sprites.drawExternalMask(16, 0, external, mask, 0, 0);
    assert(!pixel(p, 16, 0));

    p.clear();
    p.fillTriangle(2, 2, 20, 2, 11, 20, WHITE);
    assert(pixel(p, 2, 2) && pixel(p, 20, 2) && pixel(p, 11, 10));
    p.clear();
    p.drawRoundRect(2, 2, 20, 16, 4, WHITE);
    assert(pixel(p, 2, 6) && pixel(p, 10, 2) && !pixel(p, 10, 9));
    p.clear();
    p.fillRoundRect(2, 2, 20, 16, 4, WHITE);
    assert(pixel(p, 10, 10) && !pixel(p, 2, 2));
    p.clear();
    p.drawChar(0, 0, 'A', WHITE, BLACK);
    p.drawChar(8, 0, 0x7F, WHITE, BLACK); // Out-of-range glyph is ignored.
    ArduboyTonesCompat tones;
    tones.tone(440, 100);
    ArduboyPlaytuneCompat tune;
    tune.playScore(nullptr);
}

static void timingAndButtons() {
    Arduboy2Compat p;
    p.resetRuntime();
    fakeMillis=0;
    p.updateHwButton(A_BUTTON);
    assert(p.nextFrame());
    assert(p.justPressed(A_BUTTON));
    p.pollButtons(); p.pollButtons(); assert(p.justPressed(A_BUTTON));
    p.updateHwButton(0);
    fakeMillis=1; assert(!p.nextFrame());
    fakeMillis=16; assert(!p.nextFrame());
    fakeMillis=17; assert(p.nextFrame());
    assert(p.justReleased(A_BUTTON) && p.buttonUp(A_BUTTON) && p.notPressed(A_BUTTON));
    p.pollButtons(); assert(p.justReleased(A_BUTTON));
    fakeMillis=34; assert(p.nextFrame()); assert(!p.justReleased(A_BUTTON));
    p.updateHwButton(A_BUTTON|B_BUTTON);
    fakeMillis=51; assert(p.nextFrame());
    assert(p.pressed(A_BUTTON|B_BUTTON) && !p.pressed(A_BUTTON|UP_BUTTON));
    assert(p.buttonDown(A_BUTTON|UP_BUTTON));
    p.resetRuntime(); assert(p.frameCount==0 && !p.buttonsState());
    fakeMillis=100;
    assert(p.nextFrame());
    unsigned accepted=0;
    for(fakeMillis=101;fakeMillis<=1100;++fakeMillis) accepted += p.nextFrame();
    assert(accepted==60 && p.frameCount==61); // Actual 60 Hz, not integer 16ms/62.5Hz.
    assert(!p.everyXFrames(0) && p.everyXFrames(61));
    p.resetRuntime(); fakeMillis=0xFFFFFFF8U; assert(p.nextFrame());
    fakeMillis=8; assert(!p.nextFrame());
    fakeMillis=9; assert(p.nextFrame());
    fakeMillis=100000; assert(p.nextFrame()); assert(!p.nextFrame()); // No backlog spin.
    p.setFrameRate(30); assert(p.nextFrame());
    fakeMillis+=33; assert(!p.nextFrame());
    ++fakeMillis; assert(p.nextFrame());
    assert(p.generateRandomSeed() != p.generateRandomSeed());
    assert(!p.audio.enabled() && !p.isAudioEnabled());
}

static void ramRecords() {
    Arduboy2Compat a,b;
    for(int i=0;i<1024;++i) assert(a.eeprom.read(i)==0xFF);
    a.eeprom.update(-1,0); a.eeprom.update(1024,0);
    assert(a.eeprom.read(-1)==0xFF && a.eeprom.read(1024)==0xFF);
    const uint8_t values[]={1,2,3,4};
    a.eeprom.write(1022,values,4);
    uint8_t out[4]={}; a.eeprom.read(1022,out,4);
    assert(out[0]==1 && out[1]==2 && out[2]==0xFF && out[3]==0xFF);
    a.eeprom.write(768,42); a.resetRuntime();
    assert(a.eeprom.read(768)==42 && b.eeprom.read(768)==0xFF);
}

class TestApp: public ArduboyCompatApp {
public:
    Arduboy2Compat engine;
    unsigned setups=0, frames=0, calls=0;
    TestApp() { port_ptr=&engine; }
    const char* getAppName() override { return "test"; }
protected:
    void game_setup() override { ++setups; engine.beginNoLogo(); engine.setFrameRate(60); }
    void game_loop() override {
        ++calls;
        if (!engine.nextFrame()) return;
        ++frames; engine.pollButtons(); engine.clear(); engine.drawLine(0,0,10,10,WHITE); engine.display();
    }
};

static void lifecycle() {
    Canvas canvas;
    InputDriver input;
    TestApp app;
    assert(app.getMode()==APP_MODE_FRAME);
    fakeMillis=0; AmebaStore::raw_btn_mask=1U<<5;
    app.onMount(&canvas,&input); app.onUpdate(0);
    assert(app.setups==1 && app.frames==1 && app.engine.buttonsState()==0);
    AmebaStore::raw_btn_mask=0;
    fakeMillis=1; app.onUpdate(1); assert(app.frames==1);

    // A = PAT single tap (bit 4), B = PAT double tap (bit 5).
    AmebaStore::raw_btn_mask=(1U<<4);
    fakeMillis=17; app.onUpdate(17);
    assert(app.engine.buttonDown(A_BUTTON) && !app.engine.buttonPressed(B_BUTTON));
    AmebaStore::raw_btn_mask=0;
    fakeMillis=18; app.onUpdate(18);
    fakeMillis=34; app.onUpdate(34);
    fakeMillis=51; app.onUpdate(51); assert(app.engine.justReleased(A_BUTTON));

    AmebaStore::raw_btn_mask=(1U<<5);
    fakeMillis=68; app.onUpdate(68);
    assert(app.engine.buttonDown(B_BUTTON) && !app.engine.buttonPressed(A_BUTTON));

    // Directions come from tilt; the side-button bits are not used in games.
    AmebaStore::raw_btn_mask=0;
    AmebaStore::imu_ax=0.5f; AmebaStore::imu_ay=0.0f; AmebaStore::imu_az=1.0f;
    fakeMillis=85; app.onUpdate(85);
    assert(app.engine.pressed(RIGHT_BUTTON));
    assert(!app.engine.pressed(LEFT_BUTTON|UP_BUTTON|DOWN_BUTTON));
    // Crossing the axis takes one neutral frame (hysteresis), then the new side.
    AmebaStore::imu_ax=-0.5f;
    fakeMillis=102; app.onUpdate(102);
    fakeMillis=119; app.onUpdate(119);
    assert(app.engine.pressed(LEFT_BUTTON));
    AmebaStore::imu_ax=0.0f; AmebaStore::imu_ay=0.5f;
    fakeMillis=136; app.onUpdate(136);
    assert(app.engine.pressed(DOWN_BUTTON));
    AmebaStore::imu_ay=-0.5f;
    fakeMillis=153; app.onUpdate(153);
    fakeMillis=170; app.onUpdate(170);
    assert(app.engine.pressed(UP_BUTTON));
    AmebaStore::imu_ax=AmebaStore::imu_ay=0.0f;
    fakeMillis=187; app.onUpdate(187);
    assert(!app.engine.pressed(UP_BUTTON|DOWN_BUTTON|LEFT_BUTTON|RIGHT_BUTTON));
    app.engine.eeprom.write(900,23);
    app.onUnmount(); assert(app.engine.frameCount==0 && !app.engine.buttonsState());
    for(uint8_t value:app.engine.sBuffer) assert(!value);
    AmebaStore::raw_btn_mask=0;
    app.onMount(&canvas,&input);
    assert(app.setups==2 && app.engine.eeprom.read(900)==23);
    fakeMillis=52; app.onUpdate(52); assert(app.engine.frameCount==1);
    input.noisy=true;
    const unsigned polls=input.polls;
    fakeMillis=53; app.onUpdate(53); assert(input.polls-polls==16);
    app.onUnmount();

    // PAT hold >= 800 ms is the exit gesture; no side-button chord.
    TestApp holdExit;
    AmebaStore::raw_btn_mask=0; fakeMillis=0; holdExit.onMount(&canvas,&input);
    AmebaStore::raw_btn_mask=1U<<6; holdExit.onUpdate(0);
    assert(holdExit.exitRequested());
    TestApp noExit;
    AmebaStore::raw_btn_mask=0; noExit.onMount(&canvas,&input);
    AmebaStore::raw_btn_mask=(1U<<4); noExit.onUpdate(0);
    assert(!noExit.exitRequested());
    TestApp invalid; invalid.onMount(nullptr,nullptr); assert(invalid.exitRequested());
}

int main() {
    primitives(); spritesAndShapes(); timingAndButtons(); ramRecords(); lifecycle();
    std::cout << "adapter runtime passed\n";
}
