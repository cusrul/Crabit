#pragma once

#include <NanoUI.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Crabit {
namespace WatchFaces {

// App-owned state: RenderObjects borrow it only while their page is mounted.
struct TimeTransition {
    char previous[6] = "00:00";
    char current[6] = "00:00";
    uint32_t started = 0;
    bool initialized = false;
    static constexpr uint32_t duration = 420;

    void set(unsigned hour, unsigned minute, uint32_t now, bool animate = true) {
        char next[6];
        std::snprintf(next, sizeof(next), "%02u:%02u", hour % 24, minute % 60);
        if (initialized && std::strcmp(next, current) == 0)
            return;
        std::memcpy(previous, initialized && animate ? current : next, sizeof(previous));
        std::memcpy(current, next, sizeof(current));
        initialized = true;
        started = now;
    }
    float phase(uint32_t now) const {
        const uint32_t elapsed = now - started;
        return std::strcmp(previous, current) == 0 || elapsed >= duration
                   ? 1.0f
                   : static_cast<float>(elapsed) / duration;
    }
};

static const int16_t kCanvasWidth = 128;
static const int16_t kCanvasHeight = 64;
static const int16_t kTimeWidth = 90;
static const int16_t kTimeHeight = 36;
static const int16_t kSlotWidth = 18;
static const int16_t kDigitWidth = 5;
static const int16_t kDigitHeight = 7;
static const int16_t kDigitScale = 3;
static const int16_t kBitmapHeight = kDigitHeight * kDigitScale;

inline float clampPhase(float phase) {
    // The negated comparisons also make NaN deterministic (treated as phase 0).
    if (!(phase >= 0.0f))
        return 0.0f;
    if (!(phase <= 1.0f))
        return 1.0f;
    return phase;
}

namespace detail {

inline uint8_t clampMinimalDigitScale(uint8_t scale) {
    return scale < 1 ? 1 : (scale > 3 ? 3 : scale);
}

inline uint8_t clampMinimalFirstSlot(uint8_t firstSlot) {
    return firstSlot > 4 ? 4 : firstSlot;
}

inline uint8_t clampMinimalSlotCount(uint8_t slotCount, uint8_t firstSlot) {
    const uint8_t remaining = static_cast<uint8_t>(5 - firstSlot);
    if (slotCount < 1)
        return 1;
    return slotCount > remaining ? remaining : slotCount;
}

}  // namespace detail

class RenderAnimatedTime : public RenderObject {
   public:
    bool minimal;
    float phase = 1.0f;
    const TimeTransition* transition;
    float previewPhase;
    bool tightMinimal = false;
    uint8_t digitScale = kDigitScale;
    uint8_t firstSlot = 0;
    uint8_t slotCount = 5;

    RenderAnimatedTime(bool useMinimal, const TimeTransition* state, float fixedPhase = -1.0f)
        : minimal(useMinimal), transition(state), previewPhase(fixedPhase) {}

    RenderAnimatedTime(const TimeTransition* state, uint8_t scale, uint8_t first, uint8_t count,
                       float fixedPhase = -1.0f)
        : minimal(true),
          transition(state),
          previewPhase(fixedPhase),
          tightMinimal(true),
          digitScale(detail::clampMinimalDigitScale(scale)),
          firstSlot(detail::clampMinimalFirstSlot(first)),
          slotCount(detail::clampMinimalSlotCount(count, firstSlot)) {}

    void performLayout() override {
        if (tightMinimal) {
            size = constraints.constrainSize({static_cast<int16_t>(6 * digitScale * slotCount),
                                              static_cast<int16_t>(kDigitHeight * digitScale)});
        } else {
            size = constraints.constrainSize({kTimeWidth, kTimeHeight});
        }
    }

    void paintWorker(Canvas* canvas, Point origin) override {
        phase = previewPhase >= 0 ? clampPhase(previewPhase)
                                  : (transition ? transition->phase(hal_get_ticks()) : 1.0f);
        if (previewPhase < 0 && transition && phase < 1 && WindowManager::instance)
            WindowManager::instance->markDirty();
        const int16_t clipRight = NanoCoordinate(static_cast<int32_t>(origin.x) + size.width);
        const int16_t clipBottom = NanoCoordinate(static_cast<int32_t>(origin.y) + size.height);
        ScopedClip faceClip(canvas, {origin.x, origin.y, clipRight, clipBottom});
        if (!transition)
            return;
        if (minimal)
            paintMinimal(canvas, origin);
        else
            paintDotMatrix(canvas, origin);
    }

   private:
    struct Glyph {
        const uint8_t* bits;
        uint8_t width;
    };

    static Glyph lookupGlyph(char character) {
        uint8_t width = 0;
        const uint8_t* bits = get_chinese_glyph(static_cast<uint32_t>(character), 12, &width, 0);
        return {bits, width};
    }

    static bool glyphPixel(Glyph glyph, uint8_t x, uint8_t y) {
        if (!glyph.bits || x >= glyph.width || y >= 12)
            return false;
        const uint8_t rowBytes = static_cast<uint8_t>((glyph.width + 7) / 8);
        return (glyph.bits[y * rowBytes + x / 8] & (0x80u >> (x & 7))) != 0;
    }

    static bool glyphPixelInCell(Glyph glyph, uint8_t cellColumn, uint8_t row) {
        const uint8_t left = glyph.width < 6 ? static_cast<uint8_t>((6 - glyph.width) / 2) : 0;
        if (cellColumn < left)
            return false;
        return glyphPixel(glyph, static_cast<uint8_t>(cellColumn - left), row);
    }

    void paintDotMatrix(Canvas* canvas, Point origin) const {
        for (uint8_t slot = 0; slot < 5; ++slot) {
            const int16_t slotX =
                NanoCoordinate(static_cast<int32_t>(origin.x) + slot * kSlotWidth);
            ScopedClip slotClip(
                canvas, {slotX, origin.y, NanoCoordinate(static_cast<int32_t>(slotX) + kSlotWidth),
                         NanoCoordinate(static_cast<int32_t>(origin.y) + kTimeHeight)});

            const char fromCharacter = transition->previous[slot];
            const char toCharacter = transition->current[slot];
            const Glyph fromGlyph = lookupGlyph(fromCharacter);
            const Glyph toGlyph =
                fromCharacter == toCharacter ? fromGlyph : lookupGlyph(toCharacter);
            struct Dot {
                int16_t x, y;
            };
            Dot departing[72], arriving[72];
            bool used[72] = {};
            unsigned int oldCount = 0, newCount = 0;
            for (uint8_t row = 0; row < 12; ++row) {
                for (uint8_t column = 0; column < 6; ++column) {
                    const bool wasLit = glyphPixelInCell(fromGlyph, column, row);
                    const bool willLight = glyphPixelInCell(toGlyph, column, row);
                    const int16_t x = slotX + column * 3;
                    const int16_t y = origin.y + row * 3;
                    if ((phase <= 0 && wasLit) || (phase >= 1 && willLight) ||
                        (phase > 0 && phase < 1 && wasLit && willLight))
                        canvas->fillRect(x, y, 2, 2, C_WHITE);
                    if (wasLit && !willLight && oldCount < 72)
                        departing[oldCount++] = {x, y};
                    if (!wasLit && willLight && newCount < 72)
                        arriving[newCount++] = {x, y};
                }
            }
            if (phase <= 0 || phase >= 1)
                continue;
            const float eased = phase * phase * (3.0f - 2.0f * phase);
            const float ripple = std::sin(phase * 3.14159265f) * 2.0f;
            // Match changed dots to nearby destinations; common strokes remain stable.
            // All arrays are bounded by the 6x12 glyph cell; no paint-time allocation.
            for (unsigned int i = 0; i < oldCount; ++i) {
                int best = -1, distance = 100000;
                for (unsigned int j = 0; j < newCount; ++j) {
                    const int dx = arriving[j].x - departing[i].x;
                    const int dy = arriving[j].y - departing[i].y;
                    if (!used[j] && dx * dx + dy * dy < distance) {
                        best = static_cast<int>(j);
                        distance = dx * dx + dy * dy;
                    }
                }
                if (best >= 0) {
                    used[best] = true;
                    const float x = departing[i].x + eased * (arriving[best].x - departing[i].x);
                    const float y = departing[i].y + eased * (arriving[best].y - departing[i].y);
                    canvas->fillRect(static_cast<int16_t>(x + 0.5f),
                                     static_cast<int16_t>(y + (i % 2 ? ripple : -ripple) + 0.5f), 2,
                                     2, C_WHITE);
                } else if (phase < 0.65f) {
                    const int extent = phase < 0.3f ? 2 : 1;
                    canvas->fillRect(departing[i].x, departing[i].y, extent, extent, C_WHITE);
                }
            }
            if (phase > 0.35f)
                for (unsigned int j = 0; j < newCount; ++j) {
                    if (!used[j]) {
                        const int extent = phase > 0.7f ? 2 : 1;
                        canvas->fillRect(arriving[j].x, arriving[j].y, extent, extent, C_WHITE);
                    }
                }
        }
    }

    static const uint8_t* bitmapRows(char character) {
        static const uint8_t digits[10][7] = {
            {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
            {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
            {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
            {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
            {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
        };
        static const uint8_t colon[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
        static const uint8_t blank[7] = {};
        if (character == ':')
            return colon;
        return character >= '0' && character <= '9' ? digits[character - '0'] : blank;
    }

    static void paintBitmapGlyph(Canvas* canvas, int16_t slotX, int16_t glyphY, char character,
                                 uint8_t scale) {
        const uint8_t* rows = bitmapRows(character);
        const int16_t slotWidth = static_cast<int16_t>(6 * scale);
        const int16_t glyphWidth = static_cast<int16_t>(kDigitWidth * scale);
        const int16_t glyphX =
            NanoCoordinate(static_cast<int32_t>(slotX) + (slotWidth - glyphWidth) / 2);
        for (uint8_t row = 0; row < kDigitHeight; ++row) {
            for (uint8_t column = 0; column < kDigitWidth; ++column) {
                if ((rows[row] & (0x10u >> column)) == 0)
                    continue;
                const int16_t x = NanoCoordinate(static_cast<int32_t>(glyphX) + column * scale);
                const int16_t y = NanoCoordinate(static_cast<int32_t>(glyphY) + row * scale);
                canvas->fillRect(x, y, scale, scale, C_WHITE);
            }
        }
    }

    void paintMinimal(Canvas* canvas, Point origin) const {
        const uint8_t scale = tightMinimal ? digitScale : kDigitScale;
        const uint8_t first = tightMinimal ? firstSlot : 0;
        const uint8_t count = tightMinimal ? slotCount : 5;
        const int16_t slotWidth = tightMinimal ? static_cast<int16_t>(6 * scale) : kSlotWidth;
        const int16_t bitmapHeight = static_cast<int16_t>(kDigitHeight * scale);
        const int16_t glyphTop = NanoCoordinate(
            static_cast<int32_t>(origin.y) + (tightMinimal ? 0 : (kTimeHeight - bitmapHeight) / 2));
        const float eased = phase * phase * (3.0f - 2.0f * phase);
        const int16_t travel = static_cast<int16_t>(eased * bitmapHeight + 0.5f);

        for (uint8_t localSlot = 0; localSlot < count; ++localSlot) {
            const uint8_t slot = static_cast<uint8_t>(first + localSlot);
            const int16_t slotX =
                NanoCoordinate(static_cast<int32_t>(origin.x) + localSlot * slotWidth);
            ScopedClip slotClip(
                canvas, {slotX, glyphTop, NanoCoordinate(static_cast<int32_t>(slotX) + slotWidth),
                         NanoCoordinate(static_cast<int32_t>(glyphTop) + bitmapHeight)});

            const char fromCharacter = transition->previous[slot];
            const char toCharacter = transition->current[slot];
            if (fromCharacter == toCharacter) {
                paintBitmapGlyph(canvas, slotX, glyphTop, fromCharacter, scale);
                continue;
            }

            // Changed digits roll through their own clipped slot; the colon never moves.
            paintBitmapGlyph(canvas, slotX, NanoCoordinate(static_cast<int32_t>(glyphTop) - travel),
                             fromCharacter, scale);
            paintBitmapGlyph(canvas, slotX,
                             NanoCoordinate(static_cast<int32_t>(glyphTop) + bitmapHeight - travel),
                             toCharacter, scale);
        }
    }
};

class AnimatedTimeDisplay : public Widget {
    bool minimal;
    const TimeTransition* transition;

   public:
    AnimatedTimeDisplay(bool useMinimal, const TimeTransition* state)
        : minimal(useMinimal), transition(state) {}
    RenderObject* createRenderObject() override {
        return new RenderAnimatedTime(minimal, transition);
    }
};

class MinimalTimeDigits : public Widget {
    const TimeTransition* transition;
    uint8_t scale_;
    uint8_t firstSlot_;
    uint8_t slotCount_;

   public:
    MinimalTimeDigits(const TimeTransition* state, uint8_t scale = 3, uint8_t firstSlot = 0,
                      uint8_t slotCount = 5)
        : transition(state),
          scale_(detail::clampMinimalDigitScale(scale)),
          firstSlot_(detail::clampMinimalFirstSlot(firstSlot)),
          slotCount_(detail::clampMinimalSlotCount(slotCount, firstSlot_)) {}

    RenderObject* createRenderObject() override {
        return new RenderAnimatedTime(transition, scale_, firstSlot_, slotCount_);
    }
};

}  // namespace WatchFaces
}  // namespace Crabit
