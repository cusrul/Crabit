#pragma once

#include "../../firmware/shared/ui/animated_time.h"

namespace CrabitUiReview {
// Fixed-time wrapper around the same time component used by the product clock.
class PreviewAnimatedTime : public Widget {
    Crabit::WatchFaces::TimeTransition transition;
    bool minimal;
    float phase;

   public:
    PreviewAnimatedTime(bool useMinimal, float animationPhase)
        : minimal(useMinimal), phase(animationPhase) {
        transition.set(12, 59, 0, false);
        transition.set(13, 0, 0);
    }
    RenderObject* createRenderObject() override {
        return new Crabit::WatchFaces::RenderAnimatedTime(minimal, &transition, phase);
    }
};
inline Widget* buildAnimatedFace(int variant, float phase) {
    return new Container(new PreviewAnimatedTime(variant == 1, phase), 128, 64);
}
}  // namespace CrabitUiReview
