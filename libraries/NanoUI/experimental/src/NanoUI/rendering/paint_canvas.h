#pragma once

#include "geometry.h"
#include <cstdint>

namespace NanoUI {

// Adapter owns its state stack/device resources. clipRect intersects the current
// clip in translated local coordinates. No hardware or font types cross here.
class PaintCanvas {
public:
    virtual ~PaintCanvas() noexcept = default;
    virtual void save() noexcept = 0;
    virtual void restore() noexcept = 0;
    virtual void translate(Offset offset) noexcept = 0;
    virtual void clipRect(Rect rect) noexcept = 0;
    virtual void fillRect(Rect rect, std::uint32_t color) noexcept = 0;
};

} // namespace NanoUI
