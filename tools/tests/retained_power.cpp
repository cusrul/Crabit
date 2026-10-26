// Exercise the complete SysPower class with a fake MCU/board boundary.
// The separate retained_stop2 fixtures exercise the real MCU sequence template.
#define main screen_only_scenarios
#include "screen_power.cpp"
#undef main

static bool test_deep_retains_page_and_supply() {
    begin(1000, 100, 100);
    wm.stack_size = 3;
    screen_power_fake::now_ms = 1100;
    SysPower::tick();
    CHECK(SysPower::isActive());
    CHECK(screen_power_fake::count("stop2") == 1);
    CHECK(screen_power_fake::checked_voltage_calls == 0);
    CHECK(screen_power_fake::count("axp_low_power") == 0);
    CHECK(screen_power_fake::count("axp_restore") == 0);
    CHECK(screen_power_fake::count("oled_init") == 0);
    CHECK(screen_power_fake::count("input_reset") == 1);
    CHECK(wm.stack_size == 3);
    CHECK(WakeMetrics::stop_reason.load() == uint32_t(McuStop2::Reason::Qmi));
    return true;
}
static bool test_busy_usb_keeps_screen_only() {
    begin(3000, 100, 100);
    McuStop2::blocked_reason = McuStop2::Reason::Usb;
    screen_power_fake::now_ms = 3100;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    CHECK(screen_power_fake::count("stop2") == 0);
    CHECK(WakeMetrics::stop_reason.load() == uint32_t(McuStop2::Reason::Usb));
    McuStop2::blocked_reason = McuStop2::Reason::None;
    return true;
}
static bool test_disabled_deep_never_enters_stop2() {
    begin(5000, 100, 0);
    screen_power_fake::now_ms = 10000;
    SysPower::tick();
    CHECK(!SysPower::isActive());
    CHECK(screen_power_fake::count("stop2") == 0);
    return true;
}
int main() {
    // Motion during entry must not start another full screen-on interval.
    begin(10000, 100, 100);
    McuStop2::next_result = {McuStop2::Reason::MotionPending, false, 0, 80000000};
    screen_power_fake::now_ms = 10100;
    SysPower::tick();
    if (SysPower::isActive() || screen_power_fake::count("stop2") != 1 ||
        screen_power_fake::count("input_reset") != 0 ||
        screen_power_fake::count("oled_sleep_off") != 0) return EXIT_FAILURE;
    screen_power_fake::now_ms = 10500;
    SysPower::tick();
    if (SysPower::isActive() || screen_power_fake::count("stop2") != 1) return EXIT_FAILURE;
    McuStop2::next_result = {McuStop2::Reason::Qmi, true, 1, 80000000};
    screen_power_fake::now_ms = 11100;
    SysPower::tick();
    if (!SysPower::isActive() || screen_power_fake::count("stop2") != 2) return EXIT_FAILURE;

    if (!test_deep_retains_page_and_supply() || !test_busy_usb_keeps_screen_only() ||
        !test_disabled_deep_never_enters_stop2())
        return EXIT_FAILURE;
    std::cout << "Retained SysPower policy: 4 passed\n";
}
