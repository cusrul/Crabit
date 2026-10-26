#include <core/geometry.h>
#include <hal/hal_time.h>
#include <hal/input.h>

#include <cstdint>

namespace {
std::uint32_t ticks() {
    return 1234;
}
std::uint32_t delayed = 0;
void delay(std::uint32_t ms) {
    delayed = ms;
}

class ProbeInput final : public InputDriver {
   public:
    void init() override { initialized = true; }
    UIEvent poll() override { return {EVT_OK_LONG, 7}; }
    bool initialized = false;
};
}  // namespace

int main() {
    const auto constraints = BoxConstraints::tight(Size{16, 8});
    if (constraints.maxSize().width != 16 || constraints.maxSize().height != 8)
        return 1;
    const Rect first{0, 0, 10, 10};
    const Rect second{8, 8, 14, 14};
    if (!first.intersects(second) || first.width() != 10)
        return 2;

    HAL_Time::init(ticks, delay);
    if (hal_get_ticks() != 1234)
        return 3;
    hal_delay(9);
    if (delayed != 9)
        return 4;

    ProbeInput input;
    input.init();
    const auto event = input.poll();
    if (!input.initialized || event.type != EVT_OK_LONG || event.param != 7)
        return 5;
    return 0;
}
