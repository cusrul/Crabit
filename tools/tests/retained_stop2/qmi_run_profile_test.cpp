#include "qmi_run_profile.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    using namespace QmiRunProfile;
    assert(Ctrl2 == 0x25 && (Ctrl2 & 0x70) == 0x20 && SampleHz == 250);
    assert(TapTiming[0] == 9 && TapTiming[1] == 4);
    const unsigned quiet = TapTiming[2] | unsigned(TapTiming[3]) << 8;
    const unsigned window = TapTiming[4] | unsigned(TapTiming[5]) << 8;
    assert(quiet == 23 && window == 112);
    assert(std::abs(double(TapTiming[0]) * 1000 / SampleHz - 36) <= 2);
    assert(std::abs(double(quiet) * 1000 / SampleHz - 90) <= 2);
    assert(std::abs(double(window) * 1000 / SampleHz - 448) <= 2);
    assert(TapTiming[7] == 1 && TapThreshold[7] == 2);
    assert(TapThreshold[2] == 0x20 && TapThreshold[3] == 3);
    assert(TapThreshold[4] == 0x90 && TapThreshold[5] == 1);
    for (unsigned i = 0; i < 2; ++i) {
        const double oldGain = (i ? 32.0 : 8.0) / 128;
        const double expected = 1 - std::pow(1 - oldGain, 4);
        assert(std::abs(TapThreshold[i] / 128.0 - expected) <= 0.5 / 128 + 1e-9);
    }
    puts(
        "PASS: 250Hz/8g encoding, actual prior Tap windows within2ms, thresholds/priority "
        "unchanged, smoothing quantization");
}
