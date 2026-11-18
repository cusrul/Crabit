#include "controller.h"
#include "game.h"
#include "draw.h"
#include "defines.h"
#include "platform.h"
#include "font.h"

#ifdef DEBUG
#include "debug.h"
#endif

struct EngineState {
    Arduboy2Base arduboy;
    bool started = false;
};
static EngineState *g_engine = nullptr;
#define arduboy (g_engine->arduboy)
#define started (g_engine->started)

void setup() {
  // The shared adapter resets the engine; only configure it here.
  arduboy.begin();
  arduboy.setFrameRate(FRAME_RATE);
}

void loop() {
  if (!arduboy.nextFrame()) return;
  arduboy.pollButtons();

  if (!started) {
    if (arduboy.justPressed(A_BUTTON)) {
      // Original setup waited for A before seeding and loading/starting.
      arduboy.initRandomSeed();
      InitGame();
      started = true;
      return;
    }
    arduboy.clear();
    arduboy.setTextSize(2);
    arduboy.setCursor(40, 20);
    arduboy.print("2048");
    arduboy.setTextSize(1);
    arduboy.setCursor(43, 40);
    arduboy.print("PRESS A");
    arduboy.display();
    return;
  }

  StepGame();
  arduboy.display();
}

uint8_t Platform::ButtonState() {
  return arduboy.buttonsState();
}
// From Draw.h
// Mapped Arduboy Functions

void Platform::PutPixel(uint8_t x, uint8_t y, uint8_t colour)
{
  arduboy.drawPixel(x, y, colour);
}

void Platform::DrawBitmap(const uint8_t* bitmap,  int16_t x, int16_t y,
                          uint8_t w, uint8_t h, uint8_t colour) {
  arduboy.drawBitmap(x, y, bitmap, w, h, colour);
}

void Platform::DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                        uint8_t colour) {
  arduboy.drawLine(x0, y0, x1, y1, colour);
}

void Platform::DrawRect(int16_t x, int16_t y, uint8_t w, uint8_t h,
                        uint8_t colour) {
  arduboy.drawRect(x, y, w, h, colour);
}

void Platform::DrawFilledRect(int16_t x, int16_t y, uint8_t w, uint8_t h,
                              uint8_t colour) {
  arduboy.fillRect(x, y, w, h, colour);
}

void Platform::DrawCircle(int16_t x0, int16_t y0, uint8_t r, uint8_t colour) {
  arduboy.drawCircle(x0, y0, r, colour);
}

void Platform::DrawFilledCircle(int16_t x0, int16_t y0, uint8_t r,
                                uint8_t colour) {
  arduboy.fillCircle(x0, y0, r, colour);
}

void Platform::FillScreen(uint8_t colour) {
  arduboy.fillScreen(colour);
}

void Platform::Clear() {
  arduboy.clear();
}

#ifdef DEBUG
void DebugPrint(uint16_t value) {
  Serial.print(value, HEX);
  Serial.print(":");
  if (++i % 4 == 0) {
    Serial.println(" =");
  }
}

void DebugPrint(char* text) {
  Serial.println(" =");
  Serial.println(text);
  i = 0;
}
#endif

// From Game.h
int16_t Platform::Random(int16_t i0, int16_t i1) {
  return random(i0, i1);
}

uint32_t Platform::Millis() {
  return millis();
}

#define EEP(x) EEPROM[EEPROM_STORAGE_SPACE_START + x]

SavedState Platform::ToEEPROM(const uint8_t *bytes, const uint16_t offset,
                              const uint16_t sz) {
  uint16_t i;
  if (offset < 0) {
    return WrongOffset;
  }
  if (EEPROM_STORAGE_SPACE_START + offset + sz > EEPROM.length()) {
    return TooBig;
  }
  // Write Game to EEPROM

  for (i = 0; i < sz; i++) {
    EEP(i + offset) = bytes[i];
  }
  return Saved;
}

SavedState Platform::FromEEPROM(uint8_t *bytes, const uint16_t offset,
                                const uint16_t sz) {
  // Get Game from EEPROM
  uint16_t i = EEPROM_STORAGE_SPACE_START + offset;
  if (i < 0) {
    return WrongOffset;
  }
  if (i + sz > EEPROM.length()) {
    return TooBig; // Can't read from here
  }

  for (i = 0; i < sz; i++) {
    bytes[i] = EEP(i + offset);
  }
  return Saved;
}

// vim: tabstop=2:softtabstop=2:shiftwidth=2:expandtab
