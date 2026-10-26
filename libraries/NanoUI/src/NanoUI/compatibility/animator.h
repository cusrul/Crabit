#ifndef CORE_ANIMATOR_H
#define CORE_ANIMATOR_H

#include <cmath>
#include "time.h"

class Animator {
public:
    float value;
    float target;
    float speed;
    float threshold;
    uint32_t lastTick = 0;

    Animator(float startVal, float spd = 0.2)
        : value(startVal), target(startVal), speed(spd), threshold(0.01) {
        lastTick = hal_get_ticks();
    }

    void set(float t) { target = t; }

    void jumpTo(float t) {
        target = t;
        value = t;
        lastTick = hal_get_ticks();
    }

    float update() {
        uint32_t now = hal_get_ticks();
        float dt_ms = (float)(now - lastTick);
        lastTick = now;
        if (dt_ms <= 0) return value;
        if (dt_ms > 100) dt_ms = 16.6f;

        float diff = target - value;
        if (std::abs(diff) < threshold) {
            value = target;
        } else {
            // 用线性接近代替幂运算，减少 Flash 占用
            float ratio = dt_ms / 16.6f;
            float factor = speed * ratio;
            if (factor > 1.0f) factor = 1.0f;
            value += diff * factor;
        }
        return value;
    }

    bool isStable() {
        return std::abs(target - value) < threshold;
    }
};

class SpringAnimator {
public:
    float value;
    float target;
    float velocity;
    float tension;
    float friction;
    float threshold;
    uint32_t lastTick = 0;

    SpringAnimator(float startVal, float t = 0.1f, float f = 0.8f)
        : value(startVal), target(startVal), velocity(0), tension(t), friction(f), threshold(0.5f) {
        lastTick = hal_get_ticks();
    }

    void set(float t) { target = t; }

    void jumpTo(float t) {
        target = t;
        value = t;
        velocity = 0;
        lastTick = hal_get_ticks();
    }

    float update() {
        uint32_t now = hal_get_ticks();
        float dt_ms = (float)(now - lastTick);
        lastTick = now;
        if (dt_ms <= 0) return value;
        if (dt_ms > 100) dt_ms = 16.6f;

        float ratio = dt_ms / 16.6f;
        float diff = target - value;

        // 用线性衰减代替幂运算，保持时间独立性并减小体积
        velocity += diff * tension * ratio;
        float decay = 1.0f - (1.0f - friction) * ratio;
        if (decay < 0) decay = 0;
        velocity *= decay;
        value += velocity * ratio;

        if (std::abs(diff) < threshold && std::abs(velocity) < threshold) {
            value = target;
            velocity = 0;
        }
        return value;
    }

    bool isStable() {
        return std::abs(target - value) < threshold && std::abs(velocity) < threshold;
    }
};

#endif
