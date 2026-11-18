#pragma once

#include "MyArduboy.h"
#include <string.h>

#define MAX_FLASHING_MESSAGE_LENGTH 20
#define FLASHING_MESSAGE_TEXT_SIZE 1
#define FLASHING_MESSAGE_TEXT_H 8
#define FLASHING_MESSAGE_TEXT_W 6

struct FlashState {
    char *flashingMessageText;
    uint8_t flashingMessageFrame = 0;
    uint8_t flashingMessageX;
    uint8_t flashingMessageY;
};
static FlashState *g_flash = nullptr;
#define flashingMessageText (g_flash->flashingMessageText)
#define flashingMessageFrame (g_flash->flashingMessageFrame)
#define flashingMessageX (g_flash->flashingMessageX)
#define flashingMessageY (g_flash->flashingMessageY)

void startFlashingMessage(char *text, uint8_t duration) {
  size_t len = strlen(text);

  flashingMessageText = text;
  flashingMessageFrame = duration;

  flashingMessageX = (128 - (len * FLASHING_MESSAGE_TEXT_H)) / 2;
  flashingMessageY = 28;
}

void drawFlashingMessage() {
  if (flashingMessageFrame == 0) { return; }

  flashingMessageFrame--;

  if (flashingMessageFrame % 3 == 0) {
    arduboy.setCursor(flashingMessageX, flashingMessageY);
    arduboy.setTextSize(FLASHING_MESSAGE_TEXT_SIZE);
    arduboy.print(flashingMessageText);
  }
}
