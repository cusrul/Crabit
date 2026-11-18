#include "wake_log.h"
#include <cassert>
#include <cstring>
#include <cstdio>

int main() {
    using WH = WakeHistory;
    char out[112];
    assert(WH::format(out, sizeof(out), 0) > 0 && strstr(out, "END count=0"));
    for (unsigned i = 0; i < WH::Capacity + 4; ++i) WH::record(i, WH::StopResult, i, 2, 3, 4);
    assert(WH::format(out, sizeof(out), 0) > 0 && strstr(out, "s=5 "));
    WH::record(99, WH::Confirm, 9);
    assert(WH::format(out, sizeof(out), 47) > 0 && strstr(out, "s=52 "));
    assert(WH::format(out, sizeof(out), 48) > 0 && strstr(out, "END count=48 lost=4"));
    assert(WH::format(out, sizeof(out), 0) > 0 && strstr(out, "s=6 "));
    assert(WH::format(out, 8, 0) == 0);
    WH::nextSequence = UINT32_MAX - 1;
    WH::record(UINT32_MAX, WH::Confirm, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX);
    assert(WH::format(out, sizeof(out), 0) > 0);
    assert(WH::format(out, sizeof(out), 47) > 0 && strstr(out, "s=4294967295 "));
    puts("PASS: bounded ring, overflow accounting, stable snapshot and truncation");
}
