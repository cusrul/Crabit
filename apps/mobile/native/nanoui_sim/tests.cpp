#include "simulator.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

std::array<uint8_t, 1024> snapshot() {
    const uint8_t* frame = crabit_sim_frame();
    require(frame != nullptr, "active simulator exposes a frame");
    std::array<uint8_t, 1024> copy{};
    std::memcpy(copy.data(), frame, copy.size());
    return copy;
}

bool anyPixels(const std::array<uint8_t, 1024>& frame) {
    for (uint8_t pixelByte : frame) if (pixelByte) return true;
    return false;
}

void advance(uint32_t from, uint32_t to) {
    for (uint32_t now = from + 17; now <= to; now += 17) crabit_sim_step(now);
    crabit_sim_step(to);
}
}

int main() {
    require(crabit_sim_frame() == nullptr, "frame is null before start");
    require(crabit_sim_set_clock(946684799u) == -1, "clock injection rejects calls before start");
    require(crabit_sim_set_weather(0, 0, 0, 0, 0, 0) == -1,
            "weather injection rejects calls before start");
    require(crabit_sim_start() == 0, "first start succeeds");
    require(crabit_sim_start() == 0, "start is idempotent for the active host");
    const auto initial = snapshot();
    require(anyPixels(initial), "active STM32 product launcher renders a nonblank frame");
    const uint32_t firstGeneration = crabit_sim_generation();
    require(firstGeneration > 0, "initial rendering advances frame generation");

    crabit_sim_button(1, 1, 10);
    crabit_sim_step(10);
    const auto navigated = snapshot();
    require(navigated != initial, "down input changes the product NanoUI launcher frame");

    advance(10, 3000);
    const uint32_t beforeCancel = crabit_sim_generation();
    crabit_sim_button(2, 1, 3010);
    crabit_sim_button(2, -1, 3011);
    crabit_sim_step(3011);
    require(crabit_sim_generation() == beforeCancel,
            "canceled OK emits no short action and leaves rendering unchanged");

    crabit_sim_button(2, 1, 3100);
    crabit_sim_button(2, 0, 3110);
    crabit_sim_step(3110);
    advance(3110, 5000);
    const auto openedPage = snapshot();
    require(openedPage != navigated, "short OK release activates a product app entry");

    crabit_sim_button(2, 1, 5100);
    advance(5100, 5700);
    const uint32_t afterLongBack = crabit_sim_generation();
    require(afterLongBack > beforeCancel, "long OK produces the back navigation render");
    advance(5700, 7000);
    const uint32_t afterHeldPastThreshold = crabit_sim_generation();
    advance(7000, 8000);
    require(crabit_sim_generation() == afterHeldPastThreshold,
            "a continuous long hold emits BACK only once");
    crabit_sim_button(2, 0, 8001);
    crabit_sim_step(8001);
    const uint32_t afterLongRelease = crabit_sim_generation();
    advance(8001, 9000);
    require(crabit_sim_generation() == afterLongRelease,
            "long release does not emit an extra short OK");

    crabit_sim_stop();
    crabit_sim_stop();
    require(crabit_sim_frame() == nullptr, "stopped simulator releases its borrowed frame");
    require(crabit_sim_generation() == 0, "generation reports inactive after stop");
    require(crabit_sim_start() == 0, "simulator reopens after shutdown");
    require(anyPixels(snapshot()), "reopened simulator renders again");
    advance(0, 2000);
    const auto reopenedRoot = snapshot();
    crabit_sim_button(2, 1, 2010);
    crabit_sim_button(2, 0, 2020);
    advance(2020, 4000);
    require(snapshot() != reopenedRoot, "short OK opens a child before release-boundary check");
    crabit_sim_button(2, 1, 5000);
    // No tick observes the threshold. Release must still classify a long hold.
    crabit_sim_button(2, 0, 5552);
    advance(5552, 8000);
    const auto returnedRoot = snapshot();
    // The focus spring may settle at an adjacent integer pixel after a child.
    // Compare the root title/divider region rather than that animated outline.
    constexpr size_t titleOffset = 36u * 16u;
    require(std::memcmp(returnedRoot.data() + titleOffset,
                        reopenedRoot.data() + titleOffset,
                        returnedRoot.size() - titleOffset) == 0,
            "552 ms release without an intervening tick returns to root title");
    require(crabit_sim_set_clock(1792886399u) == 0, "valid local wall clock accepted");
    require(crabit_sim_show_clock() == 0, "ClockApp replaces the legacy app tree as root");
    const auto clockFrame = snapshot();
    require(clockFrame != returnedRoot, "show_clock renders ClockApp rather than Launcher");
    require(crabit_sim_set_weather(20261024u, 0, 125, 231, 20, 1792886300u) == 0,
            "valid today's weather accepted");
    require(crabit_sim_show_clock() == 0, "injected values are present when ClockApp mounts");
    const auto firstWeatherFrame = snapshot();
    require(firstWeatherFrame != clockFrame, "injected weather affects ClockApp pixels");
    const auto weatherFrame = firstWeatherFrame;
    require(crabit_sim_set_weather(20260229u, 0, 0, 0, 0, 1792886300u) == -1,
            "invalid non-leap date rejected");
    crabit_sim_step(10000);
    require(snapshot() == weatherFrame, "rejected weather leaves visible state unchanged");
    require(crabit_sim_set_weather(20261024u, 0, 232, 125, 20, 1792886300u) == -1,
            "inverted temperature range rejected");
    require(crabit_sim_set_weather(20261024u, 100, 0, 1, 0, 1792886300u) == -1,
            "unsupported WMO code rejected");
    require(crabit_sim_set_weather(0, -1, 9, 1, 101, 0) == 0,
            "date zero clears weather without validating unused fields");
    require(crabit_sim_show_clock() == 0, "ClockApp refreshes after weather clear");
    const auto noWeather = snapshot();
    require(noWeather != weatherFrame, "weather clear changes ClockApp frame");
    require(crabit_sim_set_clock(1792886399u) == 0, "clock resets immediately before midnight");
    require(crabit_sim_show_clock() == 0, "clock root remounts at the injected pre-midnight instant");
    crabit_sim_step(10000);
    const uint32_t beforeMidnight = crabit_sim_generation();
    crabit_sim_step(70000);
    require(crabit_sim_generation() > beforeMidnight,
            "clock advances across midnight and presents a new ClockApp frame");
    crabit_sim_button(2, 1, 70100);
    advance(70100, 70700);
    require(crabit_sim_generation() > beforeMidnight,
            "long BACK at ClockApp root leaves ClockApp visible");
    advance(70700, 71200);
    const auto clockRootAfterBack = snapshot();
    crabit_sim_button(2, 1, 71300);
    crabit_sim_button(2, 0, 71310);
    advance(71310, 72000);
    require(snapshot() != clockRootAfterBack,
            "short OK at ClockApp root opens its Launcher menu");
    crabit_sim_button(2, 1, 72100);
    advance(72100, 72700);
    const auto returnedClock = snapshot();
    require(returnedClock != clockRootAfterBack,
            "long BACK from Launcher returns to the ClockApp root");
    const auto stableClock = snapshot();
    crabit_sim_button(2, 1, 72800);
    crabit_sim_button(2, 0, 73400);
    crabit_sim_step(73400);
    require(snapshot() == stableClock,
            "BACK at ClockApp root cannot reveal the discarded legacy Launcher");
    require(crabit_sim_set_weather(0, 0, 0, 0, 0, 0) == 0, "no-weather state can be cleared");
    crabit_sim_stop();
    require(crabit_sim_start() == 0, "restart after injected state succeeds");
    require(crabit_sim_show_clock() == 0, "ClockApp shown after clean restart");
    const auto cleanRestart = snapshot();
    crabit_sim_step(1);
    require(snapshot() == cleanRestart, "restart does not retain injected weather or clock override");
    crabit_sim_stop();
    std::puts("PASS: STM32 product NanoUI simulator, navigation, and lifecycle");
    return 0;
}
