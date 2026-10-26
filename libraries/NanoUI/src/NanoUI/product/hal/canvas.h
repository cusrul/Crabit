#ifndef HAL_CANVAS_H
#define HAL_CANVAS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../core/geometry.h"
#include "../core/default_font.h"
#include "font_engine.h"
#include <cstdlib>

#ifndef MY_ABS
#define MY_ABS(x) ((x) < 0 ? -(x) : (x))
#endif

enum Color : uint16_t { C_BLACK = 0, C_WHITE = 1, C_INVERT = 2 };

class Canvas {
   protected:
    Rect clipRect = {0, 0, 32000, 32000};

   public:
    virtual ~Canvas() {}
    virtual void clear() = 0;
    virtual void display() = 0;

    void drawPixelWithClip(int16_t x, int16_t y, uint16_t color) {
        if (x >= clipRect.left && x < clipRect.right && y >= clipRect.top && y < clipRect.bottom)
            drawPixel(x, y, color);
    }

    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;
    virtual int16_t width() = 0;
    virtual int16_t height() = 0;

    void setClipRect(Rect r) { clipRect = r; }
    void intersectClipRect(Rect r) {
        clipRect = clipRect.intersection(r).intersection({0, 0, width(), height()});
    }
    Rect getClipRect() const { return clipRect; }
    void resetClipRect() { clipRect = {0, 0, width(), height()}; }

    virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
        int32_t dx = MY_ABS(static_cast<int32_t>(x1) - x0);
        int16_t sx = x0 < x1 ? 1 : -1;
        int32_t dy = -MY_ABS(static_cast<int32_t>(y1) - y0);
        int16_t sy = y0 < y1 ? 1 : -1;
        int32_t err = dx + dy, e2;
        while (true) {
            drawPixelWithClip(x0, y0, color);
            if (x0 == x1 && y0 == y1)
                break;
            e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
        if (w <= 0 || y < clipRect.top || y >= clipRect.bottom)
            return;
        const int32_t end = NanoMin<int32_t>(static_cast<int32_t>(x) + w, clipRect.right);
        for (int32_t i = NanoMax<int32_t>(x, clipRect.left); i < end; ++i)
            drawPixelWithClip(static_cast<int16_t>(i), y, color);
    }
    virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
        if (h <= 0 || x < clipRect.left || x >= clipRect.right)
            return;
        const int32_t end = NanoMin<int32_t>(static_cast<int32_t>(y) + h, clipRect.bottom);
        for (int32_t i = NanoMax<int32_t>(y, clipRect.top); i < end; ++i)
            drawPixelWithClip(x, static_cast<int16_t>(i), color);
    }

    virtual void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        if (w <= 0 || h <= 0)
            return;
        drawFastHLine(x, y, w, color);
        drawFastHLine(x, NanoCoordinate(static_cast<int32_t>(y) + h - 1), w, color);
        drawFastVLine(x, y, h, color);
        drawFastVLine(NanoCoordinate(static_cast<int32_t>(x) + w - 1), y, h, color);
    }

    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        if (w <= 0 || h <= 0)
            return;
        const int32_t end = NanoMin<int32_t>(static_cast<int32_t>(y) + h, clipRect.bottom);
        for (int32_t j = NanoMax<int32_t>(y, clipRect.top); j < end; ++j)
            drawFastHLine(x, static_cast<int16_t>(j), w, color);
    }

    void drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corner, uint16_t color) {
        int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
        while (x < y) {
            if (f >= 0) {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x;
            if (corner & 0x4) {
                drawPixelWithClip(x0 + x, y0 + y, color);
                drawPixelWithClip(x0 + y, y0 + x, color);
            }
            if (corner & 0x2) {
                drawPixelWithClip(x0 + x, y0 - y, color);
                drawPixelWithClip(x0 + y, y0 - x, color);
            }
            if (corner & 0x8) {
                drawPixelWithClip(x0 - y, y0 + x, color);
                drawPixelWithClip(x0 - x, y0 + y, color);
            }
            if (corner & 0x1) {
                drawPixelWithClip(x0 - y, y0 - x, color);
                drawPixelWithClip(x0 - x, y0 - y, color);
            }
        }
    }

    virtual void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
        int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
        drawPixelWithClip(x0, y0 + r, color);
        drawPixelWithClip(x0, y0 - r, color);
        drawPixelWithClip(x0 + r, y0, color);
        drawPixelWithClip(x0 - r, y0, color);
        while (x < y) {
            if (f >= 0) {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x;
            drawPixelWithClip(x0 + x, y0 + y, color);
            drawPixelWithClip(x0 - x, y0 + y, color);
            drawPixelWithClip(x0 + x, y0 - y, color);
            drawPixelWithClip(x0 - x, y0 - y, color);
            drawPixelWithClip(x0 + y, y0 + x, color);
            drawPixelWithClip(x0 - y, y0 + x, color);
            drawPixelWithClip(x0 + y, y0 - x, color);
            drawPixelWithClip(x0 - y, y0 - x, color);
        }
    }

    virtual void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
        drawFastHLine(x0 - r, y0, 2 * r + 1, color);
        int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
        while (x < y) {
            if (f >= 0) {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x;
            drawFastHLine(x0 - x, y0 + y, 2 * x + 1, color);
            drawFastHLine(x0 - x, y0 - y, 2 * x + 1, color);
            drawFastHLine(x0 - y, y0 + x, 2 * y + 1, color);
            drawFastHLine(x0 - y, y0 - x, 2 * y + 1, color);
        }
    }

    virtual void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                               uint16_t color) {
        if (w <= 0 || h <= 0)
            return;
        r = NanoMin(r, static_cast<int16_t>(NanoMin(w, h) / 2));
        if (r <= 0) {
            drawRect(x, y, w, h, color);
            return;
        }
        drawFastHLine(x + r, y, w - 2 * r, color);
        drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
        drawFastVLine(x, y + r, h - 2 * r, color);
        drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
        drawCircleHelper(x + r, y + r, r, 1, color);
        drawCircleHelper(x + w - r - 1, y + r, r, 2, color);
        drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
        drawCircleHelper(x + r, y + h - r - 1, r, 8, color);
    }

    virtual void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                               uint16_t color) {
        if (w <= 0 || h <= 0)
            return;
        if (r <= 0) {
            fillRect(x, y, w, h, color);
            return;
        }
        if (r > w / 2)
            r = w / 2;
        if (r > h / 2)
            r = h / 2;
        int16_t offsets[33];
        if (r > 32)
            r = 32;
        int16_t cx = r, cy = 0, re = 1 - r;
        while (cx >= cy) {
            offsets[r - cy] = r - cx;
            offsets[r - cx] = r - cy;
            cy++;
            if (re < 0)
                re += 2 * cy + 1;
            else {
                cx--;
                re += 2 * (cy - cx) + 1;
            }
        }
        for (int16_t i = 0; i < h; i++) {
            int16_t dx = 0;
            if (i < r)
                dx = offsets[i];
            else if (i >= h - r)
                dx = offsets[h - 1 - i];
            drawFastHLine(x + dx, y + i, w - 2 * dx, color);
        }
    }

    virtual void drawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h,
                            uint16_t color) {
        if (!bitmap)
            return;
        int16_t byteWidth = (w + 7) / 8;
        for (int16_t j = 0; j < h; j++) {
            for (int16_t i = 0; i < w; i++) {
                if (bitmap[j * byteWidth + i / 8] & (128 >> (i & 7)))
                    drawPixelWithClip(x + i, y + j, color);
            }
        }
    }
    virtual void drawBitmapPage(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h,
                                uint16_t color) {
        if (!bitmap)
            return;
        int16_t pages = (h + 7) / 8;
        for (int16_t p = 0; p < pages; p++) {
            for (int16_t i = 0; i < w; i++) {
                uint8_t data = bitmap[p * w + i];
                for (int8_t bit = 0; bit < 8; bit++) {
                    int16_t py = p * 8 + bit;
                    if (py < h && (data & (1 << bit)))
                        drawPixelWithClip(x + i, y + py, color);
                }
            }
        }
    }

    virtual void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint8_t mul) {
        for (int8_t i = 0; i < 5; i++) {
            uint8_t line = font5x7[c * 5 + i];
            for (int8_t j = 0; j < 8; j++) {
                if (line & 0x1) {
                    if (mul == 1)
                        drawPixelWithClip(x + i, y + j, color);
                    else
                        fillRect(x + i * mul, y + j * mul, mul, mul, color);
                }
                line >>= 1;
            }
        }
    }
    virtual void drawText(int16_t x, int16_t y, const char* text, uint8_t size = 12,
                          bool colorInvert = false, uint8_t font_id = 0) {
        int16_t cursorX = x;
        uint16_t color = colorInvert ? C_BLACK : C_WHITE;
        while (*text) {
            uint32_t unicode = decode_utf8(&text);
            uint8_t glyph_w;
            const uint8_t* glyph = get_chinese_glyph(unicode, size, &glyph_w, font_id);
            if (glyph) {
                drawBitmap(cursorX, y, glyph, glyph_w, size, color);
                cursorX += glyph_w;
            } else if (unicode < 128) {
                uint8_t mul = (size >= 16) ? (size / 8) : 1;
                drawChar(cursorX, y, (unsigned char)unicode, color, mul);
                cursorX += 6 * mul;
            } else {
                drawRect(cursorX, y, size, size, color);
                cursorX += size;
            }
        }
    }
    virtual void drawTextInRect(int16_t x, int16_t y, int16_t w, int16_t h, const char* text,
                                int16_t offset, uint8_t size = 12, uint8_t font_id = 0) {
        if (!text)
            return;
        Rect oldClip = getClipRect();
        intersectClipRect({x, y, NanoCoordinate((int32_t)x + w), NanoCoordinate((int32_t)y + h)});
        drawText(x - offset, y, text, size, false, font_id);
        setClipRect(oldClip);
    }
};

// Nested clipping is intersected, then restored, without a heap allocation.
class ScopedClip {
    Canvas* canvas;
    Rect previous;

   public:
    ScopedClip(Canvas* c, Rect bounds) : canvas(c), previous(c->getClipRect()) {
        canvas->intersectClipRect(bounds);
    }
    ~ScopedClip() { canvas->setClipRect(previous); }
    ScopedClip(const ScopedClip&) = delete;
    ScopedClip& operator=(const ScopedClip&) = delete;
};

#endif
