#pragma once

// JumpBoy's Arduboy2 helper. The shared compatibility layer supplies the
// framebuffer, buttons and silent audio; drawSlowXYBitmap is the one original
// Arduboy2 primitive this game uses that the port layer does not expose.
// The body is the upstream Arduboy2 implementation (MIT), unchanged apart
// from being a member here and reading through pgm_read_byte.
class JumpBoyArduboy : public Arduboy2Compat {
public:
    void drawSlowXYBitmap(int16_t x, int16_t y, const uint8_t *bitmap,
                          uint8_t w, uint8_t h, uint8_t color = WHITE) {
        // no need to draw at all if we're offscreen
        if (x + w < 0 || x > WIDTH - 1 || y + h < 0 || y > HEIGHT - 1)
            return;

        int16_t xi, yi, byteWidth = (w + 7) / 8;
        for (yi = 0; yi < h; yi++) {
            for (xi = 0; xi < w; xi++) {
                if (pgm_read_byte(bitmap + yi * byteWidth + xi / 8) & (128 >> (xi & 7))) {
                    drawPixel(x + xi, y + yi, color);
                }
            }
        }
    }
};
