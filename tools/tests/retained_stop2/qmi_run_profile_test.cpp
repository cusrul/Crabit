#include "qmi_run.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    using QP = QmiRunProfile;
    assert(QP::Ctrl2 == 0x25 && (QP::Ctrl2 & 0x70) == 0x20 && QP::SampleHz == 250);
    assert(QP::TapTiming[0] == 9 && QP::TapTiming[1] == 4);
    const unsigned quiet = QP::TapTiming[2] | unsigned(QP::TapTiming[3]) << 8;
    const unsigned window = QP::TapTiming[4] | unsigned(QP::TapTiming[5]) << 8;
    assert(quiet == 23 && window == 112);
    assert(std::abs(double(QP::TapTiming[0]) * 1000 / QP::SampleHz - 36) <= 2);
    assert(std::abs(double(quiet) * 1000 / QP::SampleHz - 90) <= 2);
    assert(std::abs(double(window) * 1000 / QP::SampleHz - 448) <= 2);
    assert(QP::TapTiming[7] == 1 && QP::TapThreshold[7] == 2);
    assert(QP::TapThreshold[2] == 0x20 && QP::TapThreshold[3] == 3);
    assert(QP::TapThreshold[4] == 0x90 && QP::TapThreshold[5] == 1);
    for (unsigned i = 0; i < 2; ++i) {
        const double oldGain = (i ? 32.0 : 8.0) / 128;
        const double expected = 1 - std::pow(1 - oldGain, 4);
        assert(std::abs(QP::TapThreshold[i] / 128.0 - expected) <= 0.5 / 128 + 1e-9);
    }
    puts(
        "PASS: 250Hz/8g encoding, actual prior Tap windows within2ms, thresholds/priority "
        "unchanged, smoothing quantization");
}
