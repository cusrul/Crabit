#pragma once

#include <NanoUI/framework/types.h>
#include <cmath>

namespace NanoUI {

class ScrollController final {
public:
    float offset() const noexcept { return offset_; }

    UiError setOffset(float value, float viewport, float extent) noexcept {
        if (!std::isfinite(value) || !std::isfinite(viewport) ||
            !std::isfinite(extent) || value < 0.0f || viewport <= 0.0f ||
            extent < 0.0f)
            return UiError::invalidInput;
        offset_ = clamp(value, extent - viewport);
        return UiError::none;
    }

    UiError reveal(float start, float end, float viewport, float extent) noexcept {
        if (!validGeometry(start, end, viewport, extent) || end > extent)
            return UiError::invalidInput;

        float target = offset_;
        if (end - start > viewport) {
            target = start;
        } else if (start < offset_) {
            target = start;
        } else if (end > offset_ + viewport) {
            target = end - viewport;
        }
        offset_ = clamp(target, extent - viewport);
        return UiError::none;
    }

private:
    static bool validGeometry(float start, float end, float viewport,
                              float extent) noexcept {
        return std::isfinite(start) && std::isfinite(end) &&
               std::isfinite(viewport) && std::isfinite(extent) &&
               start >= 0.0f && end >= start && viewport > 0.0f &&
               extent >= 0.0f && end <= extent;
    }

    static float clamp(float value, float maximum) noexcept {
        if (maximum < 0.0f) maximum = 0.0f;
        if (value < 0.0f) return 0.0f;
        return value > maximum ? maximum : value;
    }

    float offset_ = 0.0f;
};

} // namespace NanoUI
