#include "idle_sleep_policy.h"
#include <cassert>
#include <cstdio>
int main() {
    assert(!IdleSleep::due(false, 3000, 10000, 2999));
    assert(IdleSleep::due(false, 3000, 10000, 3000));
    assert(IdleSleep::due(false, 3000, 1, 3000));
    assert(!IdleSleep::due(false, 3000, 0, 3000));
    assert(!IdleSleep::due(true, 3000, 10000, 3000));
    assert(!IdleSleep::due(false, 0, 10000, 3000));
    assert(IdleSleep::due(false, UINT32_MAX - 1, 10000, UINT32_MAX));
    IdleSleep::WakeInputGuard g;
    assert(!g.consume(0, false));
    g.arm(100);
    assert(g.consume(200, false));
    assert(g.consume(300, true));
    assert(g.consume(1000, true));  // Holding wake key never becomes a new event.
    assert(g.consume(1001, false));
    assert(!g.consume(1002, false));
    g.arm(UINT32_MAX - 100);
    assert(g.consume(50, false));
    assert(g.consume(101, false));
    assert(!g.consume(102, false));
    puts(
        "PASS: direct screen deadline, legacy disable/screen-only, long idle, wake key "
        "release/200ms guard, tick wrap");
}
