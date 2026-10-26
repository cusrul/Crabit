#pragma once
#include <NanoUI/widgets/decoration.h>

namespace NanoUI {
// Monochrome default. Color ports can supply a different value, not different widgets.
struct Theme {
    std::uint32_t background = 0x000000FFu;
    std::uint32_t foreground = 0xFFFFFFFFu;
    std::uint32_t selectedBackground = 0xFFFFFFFFu;
    std::uint32_t selectedForeground = 0x000000FFu;
    float outerInset = 3;
    float rowHeight = 15;
    float gap = 1;
    float textHeight = 7;
};
} // namespace NanoUI
