#include "wake_history.h"
#include <cassert>
#include <cstring>
#include <cstdio>

int main() {
    using namespace WakeHistory;
    char out[112];
    assert(format(out, sizeof(out), 0) > 0 && strstr(out, "END count=0"));
    for (unsigned i = 0; i < Capacity + 4; ++i) record(i, StopResult, i, 2, 3, 4);
    assert(format(out, sizeof(out), 0) > 0 && strstr(out, "s=5 "));
    record(99, Confirm, 9);
    assert(format(out, sizeof(out), 47) > 0 && strstr(out, "s=52 "));
    assert(format(out, sizeof(out), 48) > 0 && strstr(out, "END count=48 lost=4"));
    assert(format(out, sizeof(out), 0) > 0 && strstr(out, "s=6 "));
    assert(format(out, 8, 0) == 0);
    nextSequence = UINT32_MAX - 1;
    record(UINT32_MAX, Confirm, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX);
    assert(format(out, sizeof(out), 0) > 0);
    assert(format(out, sizeof(out), 47) > 0 && strstr(out, "s=4294967295 "));
    puts("PASS: bounded ring, overflow accounting, stable snapshot and truncation");
}
