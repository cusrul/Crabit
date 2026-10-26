#include "wake_diag_format.h"
#include "wake_metrics.h"
#include <cassert>
#include <cstring>
#include <initializer_list>

int main() {
    char text[32];
    using WakeDiag::Row;
    assert(WakeDiag::format(text, sizeof text, Row::Clock, 80000000));
    assert(strcmp(text, "80 MHz") == 0);
    assert(WakeDiag::format(text, sizeof text, Row::UiWait, 100, 9));
    assert(strcmp(text, "100ms/9") == 0);
    assert(WakeDiag::format(text, sizeof text, Row::Sensor, 82));
    assert(strcmp(text, "82ms") == 0);
    assert(WakeDiag::format(text, sizeof text, Row::I2c, 3, 0x20));
    assert(strcmp(text, "3/0x20") == 0);
    const char* states[] = {"NONE/1", "DEF/1", "ADDR/1", "CFG/1", "SUSP/1"};
    for (unsigned i = 0; i < 5; ++i) {
        assert(WakeDiag::format(text, sizeof text, Row::Usb, i, 1));
        assert(strcmp(text, states[i]) == 0);
    }
    for (Row row : {Row::Clock, Row::UiWait, Row::Sensor, Row::I2c, Row::Usb})
        assert(WakeDiag::format(text, sizeof text, row, UINT32_MAX, UINT32_MAX));
    char small[3] = {'x', 'x', 'z'};
    assert(!WakeDiag::format(small, 2, Row::UiWait, 100, 0));
    assert(small[1] == '\0' && small[2] == 'z');
    WakeMetrics::maximum(WakeMetrics::ui_wait_max, 10);
    WakeMetrics::maximum(WakeMetrics::ui_wait_max, 5);
    assert(WakeMetrics::ui_wait_max.load(std::memory_order_relaxed) == 10);
    assert(WakeMetrics::recoveries.load(std::memory_order_relaxed) == 0);
    puts("PASS: five rows, USB states, maximum counters, UINT32 limits and buffer bounds");
}
