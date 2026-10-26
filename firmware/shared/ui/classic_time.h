#pragma once

#include "animated_time.h"

namespace Crabit {
namespace WatchFaces {

class RenderClassicTime : public RenderObject {
   public:
    explicit RenderClassicTime(const TimeTransition* state) : transition(state) {}

    void performLayout() override { size = constraints.constrainSize({kWidth, kHeight}); }

    void paintWorker(Canvas* canvas, Point origin) override {
        if (!canvas)
            return;

        const int16_t right = coordinate(origin.x, size.width);
        const int16_t bottom = coordinate(origin.y, size.height);
        ScopedClip faceClip(canvas, {origin.x, origin.y, right, bottom});

        const bool valid = hasValidTime();
        for (uint8_t slot = 0; slot < 4; ++slot) {
            const int16_t x = coordinate(origin.x, digitOffset(slot));
            const char digit = valid ? transition->current[slot < 2 ? slot : slot + 1] : '-';
            paintDigit(canvas, x, origin.y, digit);
        }
        paintColon(canvas, coordinate(origin.x, 46), origin.y);
    }

   private:
    enum : uint8_t {
        kTop = 1u << 0,
        kUpperRight = 1u << 1,
        kLowerRight = 1u << 2,
        kBottom = 1u << 3,
        kLowerLeft = 1u << 4,
        kUpperLeft = 1u << 5,
        kMiddle = 1u << 6,
    };

    static constexpr int16_t kWidth = 94;
    static constexpr int16_t kHeight = 28;
    static constexpr int16_t kDigitWidth = 16;
    static constexpr int16_t kHorizontalLength = 12;
    static constexpr int16_t kVerticalLength = 10;

    const TimeTransition* transition;

    static int16_t coordinate(int16_t start, int32_t delta) {
        return NanoCoordinate(static_cast<int32_t>(start) + delta);
    }

    bool hasValidTime() const {
        if (!transition || transition->current[5] != '\0' || transition->current[2] != ':')
            return false;
        return isDigit(transition->current[0]) && isDigit(transition->current[1]) &&
               isDigit(transition->current[3]) && isDigit(transition->current[4]);
    }

    static bool isDigit(char value) { return value >= '0' && value <= '9'; }

    static int16_t digitOffset(uint8_t slot) {
        switch (slot) {
            case 0:
                return 1;
            case 1:
                return 21;
            case 2:
                return 57;
            default:
                return 77;
        }
    }

    static uint8_t segmentsFor(char digit) {
        switch (digit) {
            case '0':
                return kTop | kUpperRight | kLowerRight | kBottom | kLowerLeft | kUpperLeft;
            case '1':
                return kUpperRight | kLowerRight;
            case '2':
                return kTop | kUpperRight | kMiddle | kLowerLeft | kBottom;
            case '3':
                return kTop | kUpperRight | kMiddle | kLowerRight | kBottom;
            case '4':
                return kUpperLeft | kUpperRight | kMiddle | kLowerRight;
            case '5':
                return kTop | kUpperLeft | kMiddle | kLowerRight | kBottom;
            case '6':
                return kTop | kUpperLeft | kMiddle | kLowerRight | kBottom | kLowerLeft;
            case '7':
                return kTop | kUpperRight | kLowerRight;
            case '8':
                return kTop | kUpperRight | kLowerRight | kBottom | kLowerLeft | kUpperLeft |
                       kMiddle;
            case '9':
                return kTop | kUpperRight | kLowerRight | kBottom | kUpperLeft | kMiddle;
            default:
                return kMiddle;
        }
    }

    static bool hasSegment(uint8_t segments, uint8_t segment) { return (segments & segment) != 0; }

    static void paintHorizontal(Canvas* canvas, int16_t x, int16_t y) {
        canvas->fillRect(coordinate(x, 1), y, kHorizontalLength - 2, 2, C_WHITE);
        canvas->drawLine(x, coordinate(y, 1), coordinate(x, 1), y, C_WHITE);
        canvas->drawLine(coordinate(x, kHorizontalLength - 2), y,
                         coordinate(x, kHorizontalLength - 1), coordinate(y, 1), C_WHITE);
    }

    static void paintVertical(Canvas* canvas, int16_t x, int16_t y) {
        canvas->fillRect(x, coordinate(y, 1), 2, kVerticalLength - 2, C_WHITE);
        canvas->drawLine(coordinate(x, 1), y, x, coordinate(y, 1), C_WHITE);
        canvas->drawLine(x, coordinate(y, kVerticalLength - 2), coordinate(x, 1),
                         coordinate(y, kVerticalLength - 1), C_WHITE);
    }

    static void paintDigit(Canvas* canvas, int16_t x, int16_t y, char digit) {
        const uint8_t segments = segmentsFor(digit);
        if (hasSegment(segments, kTop))
            paintHorizontal(canvas, coordinate(x, 2), y);
        if (hasSegment(segments, kUpperRight))
            paintVertical(canvas, coordinate(x, kDigitWidth - 2), coordinate(y, 2));
        if (hasSegment(segments, kLowerRight))
            paintVertical(canvas, coordinate(x, kDigitWidth - 2), coordinate(y, 15));
        if (hasSegment(segments, kBottom))
            paintHorizontal(canvas, coordinate(x, 2), coordinate(y, 26));
        if (hasSegment(segments, kLowerLeft))
            paintVertical(canvas, x, coordinate(y, 15));
        if (hasSegment(segments, kUpperLeft))
            paintVertical(canvas, x, coordinate(y, 2));
        if (hasSegment(segments, kMiddle))
            paintHorizontal(canvas, coordinate(x, 2), coordinate(y, 13));
    }

    static void paintColon(Canvas* canvas, int16_t x, int16_t y) {
        canvas->fillRect(x, coordinate(y, 8), 2, 2, C_WHITE);
        canvas->fillRect(x, coordinate(y, 18), 2, 2, C_WHITE);
    }
};

class ClassicTimeDisplay : public Widget {
   public:
    explicit ClassicTimeDisplay(const TimeTransition* state) : transition(state) {}

    RenderObject* createRenderObject() override { return new RenderClassicTime(transition); }

   private:
    const TimeTransition* transition;
};

}  // namespace WatchFaces
}  // namespace Crabit
