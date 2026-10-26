#pragma once
#include <array>
#include <stdint.h>

namespace QmiRunProfile {
inline constexpr unsigned SampleHz = 250;
inline constexpr uint8_t Ctrl2 = 0x25;  // +/-8g, accel-only 250Hz, 4096 LSB/g.
constexpr unsigned samples(unsigned milliseconds) {
    return (milliseconds * SampleHz + 500u) / 1000u;
}
// Preserve the OLD actual 1000Hz windows: 36ms peak, 90ms quiet, 448ms double.
inline constexpr std::array<uint8_t, 8> TapTiming = {
    samples(36), 4, samples(90) & 0xFF, samples(90) >> 8, samples(448) & 0xFF, samples(448) >> 8,
    0,           1};
// At a four-times longer sampling interval, preserve approximate smoothing:
// new gain = 1 - (1 - old gain)^4, quantized to 1/128.
// Alpha8/128 ->29/128; Gamma32/128 ->88/128. Physical thresholds unchanged.
inline constexpr std::array<uint8_t, 8> TapThreshold = {29, 88, 0x20, 3, 0x90, 1, 0, 2};
static_assert(SampleHz > 200, "QST recommends Tap ODR above 200Hz");
}  // namespace QmiRunProfile
