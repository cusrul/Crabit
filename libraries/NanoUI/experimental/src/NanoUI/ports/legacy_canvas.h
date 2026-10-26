#pragma once
#include "pixel_canvas.h"

namespace NanoUI {
// No legacy includes here: the caller selects its existing platform Canvas type.
// Requires width/height/getClipRect/drawPixel with the existing Crabit signatures.
template<class CanvasType>
class LegacyCanvasBridge final : public PixelCanvas {
public:
    explicit LegacyCanvasBridge(CanvasType& canvas) noexcept
        : PixelCanvas(canvas.width(), canvas.height(), PixelSink(write, &canvas), clip(canvas)),
          canvas_(canvas) {}
    bool beginFrame() noexcept { return PixelCanvas::beginFrame(clip(canvas_)); }
private:
    static Rect clip(const CanvasType& canvas) noexcept {
        const auto r = canvas.getClipRect();
        return Rect(static_cast<float>(r.left), static_cast<float>(r.top),
                    static_cast<float>(r.right), static_cast<float>(r.bottom));
    }
    static void write(int x, int y, std::uint32_t rgba, void* payload) noexcept {
        const unsigned brightness = ((rgba >> 24) & 255) + ((rgba >> 16) & 255) + ((rgba >> 8) & 255);
        // Existing monochrome Canvas uses BLACK=0, WHITE=1. Never invert or flush.
        static_cast<CanvasType*>(payload)->drawPixel(static_cast<std::int16_t>(x),
            static_cast<std::int16_t>(y), static_cast<std::uint16_t>(brightness >= 384 ? 1 : 0));
    }
    CanvasType& canvas_;
};
} // namespace NanoUI
