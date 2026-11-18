#pragma once

// Include-only staging for FrankVanDeVen/Montana at
// 35810a194a4cbe2ae71d4fb663dc78ebd32cb218 (root MIT). The upstream sketch
// used Arduboy2 + Sprites and six blocking screens; this port keeps the 52-card
// rules, deal/shuffle/hint/win/lose code and all data.h assets, and expresses
// the presentation screens as frame states.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace MontanaSandbox {

class MontanaArduboy : public Arduboy2Compat {
public:
    size_t println(const char *text) {
        size_t n = print(text);
        return n + print('\n');
    }
    size_t println(const __FlashStringHelper *text) {
        size_t n = print(text);
        return n + print('\n');
    }

    // Local implementation of the Arduboy2 RLE format used by data.h's splash.
    // The compressed bytes are retained unchanged; only this decoder is local.
    // Format/behaviour follow Arduboy2 6.0.0 drawCompressed (BSD-3-Clause,
    // Scott Allen and contributors); see README for the attribution notice.
    void drawCompressed(int16_t sx, int16_t sy, const uint8_t *bitmap,
                        uint8_t color = WHITE) {
        struct BitReader {
            const uint8_t *source;
            uint16_t sourceIndex;
            uint8_t bitBuffer;
            uint8_t byteBuffer;
            uint16_t read(uint16_t bitCount) {
                uint16_t result = 0;
                for (uint16_t i = 0; i < bitCount; ++i) {
                    if (bitBuffer == 0) {
                        bitBuffer = 1;
                        byteBuffer = pgm_read_byte(source + sourceIndex);
                        ++sourceIndex;
                    }
                    if (byteBuffer & bitBuffer) result |= (1 << i);
                    bitBuffer <<= 1;
                }
                return result;
            }
        } cs;
        cs.source = bitmap;
        cs.sourceIndex = 0;
        cs.bitBuffer = 0;
        cs.byteBuffer = 0;

        int width = (int) cs.read(8) + 1;
        int height = (int) cs.read(8) + 1;
        uint8_t spanColour = (uint8_t) cs.read(1);
        if ((sx + width <= 0) || (sx > WIDTH - 1) ||
                (sy + height <= 0) || (sy > HEIGHT - 1)) return;

        int yOffset = (sy < 0 ? -sy : sy) % 8;
        int startRow = sy / 8;
        if (sy < 0) {
            --startRow;
            yOffset = 8 - yOffset;
        }
        int rows = height / 8;
        if (height % 8) ++rows;

        int rowOffset = 0, columnOffset = 0;
        uint8_t byte = 0;
        uint8_t bit = 1;
        while (rowOffset < rows) {
            uint16_t bitLength = 1;
            while (cs.read(1) == 0) bitLength += 2;
            uint16_t len = cs.read(bitLength) + 1;
            for (uint16_t i = 0; i < len; ++i) {
                if (spanColour != 0) byte |= bit;
                bit <<= 1;
                if (bit == 0) {
                    int bRow = startRow + rowOffset;
                    if ((bRow <= (HEIGHT / 8) - 1) && (bRow > -2) &&
                            (columnOffset + sx <= WIDTH - 1) &&
                            (columnOffset + sx >= 0)) {
                        int16_t offset = bRow * WIDTH + sx + columnOffset;
                        if (bRow >= 0) {
                            uint8_t value = byte << yOffset;
                            if (color != 0) sBuffer[offset] |= value;
                            else sBuffer[offset] &= ~value;
                        }
                        if ((yOffset != 0) && (bRow < (HEIGHT / 8) - 1)) {
                            int16_t index = offset + WIDTH;
                            uint8_t value = byte >> (8 - yOffset);
                            if (color != 0) sBuffer[index] |= value;
                            else sBuffer[index] &= ~value;
                        }
                    }
                    ++columnOffset;
                    if (columnOffset >= width) {
                        columnOffset = 0;
                        ++rowOffset;
                    }
                    byte = 0;
                    bit = 1;
                }
            }
            spanColour ^= 1;
        }
    }
};

struct MontanaSprites {
    static void drawOverwrite(int16_t x, int16_t y, const uint8_t *image,
                              uint8_t frame);
};

#define Sprites MontanaSprites
#include "data.h"
#include "montana.inc.h"
#undef Sprites

void MontanaSprites::drawOverwrite(int16_t x, int16_t y, const uint8_t *image,
                                   uint8_t frame) {
    SpritesCompat sprites(arduboy);
    sprites.drawOverwrite(x, y, image, frame);
}

void resetGameState() {
    // begin() resets the shared engine; clear everything the sketch keeps in
    // addition to it so reentry starts at the intro with no previous transient.
    wacht = 0;
    knipper = 0;
    snel = 10;
    for (int i = 0; i < 52; ++i) stok[i] = (byte) i;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 13; ++x) spel[x][y] = 99;
    x = y = 0;
    Cx = Cy = 0;
    game = 0;
    card = 0;
    hint = 2;
    eind = 2;
    delen = 3;
    tip = 0;
    introPhase = 0;
    introCredit = false;
    optionPhase = 0;
    losePhase = 0;
    winPhase = 0;
    hintVerder = 0;
    hintKnipper = 0;
    hintWacht = 0;
}

} // namespace MontanaSandbox

#undef APP_TITLE
#undef COOKIES
#undef ANIM_MAX
#undef PL_SIZE
#undef CK_SIZE
#undef SCALE
#undef WIDTH_S
#undef HEIGHT_S
#undef PL_SIZE_S
#undef CK_SIZE_S
#undef HIT_DIFF_S
#undef NEW_DIFF_S
#undef GAME_DURATION
#undef IMG_TITLE_W
#undef IMG_TITLE_H
#undef IMG_START1_W
#undef IMG_START1_H
#undef IMG_START2_W
#undef IMG_START2_H
#undef IMG_OVER_W
#undef IMG_OVER_H
#undef IMG_FIGURE_W
#undef IMG_FIGURE_H
#undef IMG_TEN_W
#undef IMG_TEN_H
#undef CLOCK_R
#undef CLOCK_X
#undef CLOCK_Y
#undef LED_LEVEL_START
#undef LED_VALUE
#undef wachttijd
#undef wachttijd2
#undef wachttijd3
#undef wachttijd4
#undef wachttijd5
#undef dprint
#undef dprintln
