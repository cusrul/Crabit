// The Python fixture pre-includes host doubles for hw_ws2812.h and main.h,
// using their production include guards before this unmodified production header.
#include "../../firmware/stm32/Core/Inc/input_feedback.h"

#include <cassert>
#include <cstdint>

uint32_t now = 0;
uint32_t HAL_GetTick() {
    return now;
}
WS2812_Driver led_drv;

namespace {

void testPulseDeadlineAcrossTickWrap() {
    using namespace CrabitPreferences;

    load(Record{});
    InputFeedback::setManual(false);
    led_drv = WS2812_Driver{};
    now = UINT32_MAX - 19u;

    InputFeedback::press();
    assert(led_drv.show_calls == 1);
    assert(led_drv.brightness == 8);
    assert(led_drv.pixels[0].r == 255 && led_drv.pixels[0].g == 255 && led_drv.pixels[0].b == 255);

    now += 39u;
    InputFeedback::tick();
    assert(led_drv.clear_calls == 0);
    assert(led_drv.pixels[0].r == 255);

    now += 1u;
    InputFeedback::tick();
    assert(led_drv.clear_calls == 1);
    assert(led_drv.show_calls == 2);
    assert(led_drv.brightness == 0);
    assert(led_drv.pixels[0].r == 0 && led_drv.pixels[0].g == 0 && led_drv.pixels[0].b == 0);
}

void testPreferenceAndManualOwnershipGates() {
    using namespace CrabitPreferences;

    InputFeedback::setManual(false);
    led_drv = WS2812_Driver{};
    current.flags = static_cast<uint8_t>(Pat | Qmi | Sound);
    current.led_level = 8;
    InputFeedback::press();
    now += 40u;
    InputFeedback::tick();
    assert(led_drv.show_calls == 0);
    assert(led_drv.clear_calls == 0);

    current.flags |= Led;
    current.led_level = 0;
    InputFeedback::press();
    assert(led_drv.show_calls == 0);

    current.led_level = 47;
    InputFeedback::setManual(true);
    InputFeedback::press();
    assert(led_drv.show_calls == 0);
    led_drv.setBrightness(41);
    led_drv.setLED(0, 12, 34, 56);
    led_drv.show();
    const unsigned manual_show_count = led_drv.show_calls;
    const unsigned manual_clear_count = led_drv.clear_calls;

    InputFeedback::setManual(false);
    InputFeedback::press();
    assert(led_drv.show_calls == manual_show_count + 1);
    assert(led_drv.pixels[0].r == 255);

    // Transfer ownership after the feedback pulse: expiry must not restore or
    // clear over the manual UI color.
    InputFeedback::setManual(true);
    led_drv.setBrightness(41);
    led_drv.setLED(0, 12, 34, 56);
    led_drv.show();
    now += 40u;
    InputFeedback::tick();
    assert(led_drv.clear_calls == manual_clear_count);
    assert(led_drv.show_calls == manual_show_count + 2);
    assert(led_drv.brightness == 41);
    assert(led_drv.pixels[0].r == 12 && led_drv.pixels[0].g == 34 && led_drv.pixels[0].b == 56);
}

}  // namespace

int main() {
    testPulseDeadlineAcrossTickWrap();
    testPreferenceAndManualOwnershipGates();
    return 0;
}
