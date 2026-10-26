#include <core/animator.h>

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
std::uint32_t now = 0;
std::uint32_t ticks() {
    return now;
}
int checks = 0;
int failures = 0;
#define CHECK(value)                                   \
    do {                                               \
        ++checks;                                      \
        if (!(value)) {                                \
            ++failures;                                \
            std::cerr << __LINE__ << ": " #value "\n"; \
        }                                              \
    } while (false)
bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.0001f;
}

void testAnimatorTimeAndLimits() {
    now = 42;
    Animator animator(0);
    CHECK(animator.lastTick == 42);
    animator.set(10);
    CHECK(!animator.isStable());
    CHECK(near(animator.update(), 0));  // No elapsed time.
    now += 16;
    CHECK(near(animator.update(), 1.9277108f));
    CHECK(animator.lastTick == now);

    animator.jumpTo(0);
    animator.set(10);
    now += 1001;
    CHECK(near(animator.update(), 2));  // Existing long-pause fallback: 16.6 ms.
    animator.jumpTo(0);
    animator.set(10);
    now += 100;
    CHECK(near(animator.update(), 10));  // Existing factor upper clamp.
    CHECK(animator.isStable());
    animator.jumpTo(7);
    CHECK(animator.value == 7 && animator.target == 7 && animator.lastTick == now);
    animator.set(7.005f);
    ++now;
    CHECK(animator.update() == animator.target);

    now = UINT32_MAX - 7;
    animator.jumpTo(0);
    animator.set(10);
    now = 8;
    CHECK(near(animator.update(), 1.9277108f));  // Unsigned tick wrap, 16 ms.
}

void testSpringTimeAndState() {
    now = 100;
    SpringAnimator spring(0);
    spring.set(10);
    CHECK(!spring.isStable());
    CHECK(near(spring.update(), 0) && spring.velocity == 0);
    now += 200;
    CHECK(near(spring.update(), 0.8f));
    CHECK(near(spring.velocity, 0.8f));
    CHECK(spring.lastTick == now);
    spring.jumpTo(3);
    CHECK(spring.value == 3 && spring.target == 3 && spring.velocity == 0);
    CHECK(spring.isStable());
    spring.set(3.1f);
    now += 200;
    CHECK(spring.update() == spring.target && spring.velocity == 0);

    now = UINT32_MAX - 7;
    spring.jumpTo(0);
    spring.set(10);
    now = 8;
    CHECK(near(spring.update(), 0.7499296f));
    CHECK(near(spring.velocity, 0.7780520f));
    const float value = spring.value;
    const float velocity = spring.velocity;
    CHECK(spring.update() == value && spring.velocity == velocity);
}
}  // namespace

int main() {
    HAL_Time::init(ticks, nullptr);
    testAnimatorTimeAndLimits();
    testSpringTimeAndState();
    HAL_Time::init(nullptr, nullptr);
    std::cout << "Legacy animation: " << checks - failures << '/' << checks << " checks passed\n";
    return failures ? 1 : 0;
}
