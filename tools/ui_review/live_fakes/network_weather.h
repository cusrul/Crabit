#pragma once
#include "../../../firmware/shared/weather_contract.h"

// Simulated cached forecasts only. No socket, phone or radio request.
namespace Crabit {
namespace NetworkWeather {
inline unsigned fakeCount = 1;
inline CrabitWeather::Day fakeDays[CrabitWeather::MaxDays] = {{20261022, 0, 180, 260, 10}};
inline unsigned phoneCount() {
    return fakeCount;
}
inline const CrabitWeather::Day& phoneDay(unsigned i) {
    return fakeDays[i < fakeCount ? i : 0];
}
inline const char* condition(uint16_t code) {
    if (code == 0)
        return "晴";
    if (code <= 3)
        return "多云";
    if (code == 45 || code == 48)
        return "雾";
    if (code >= 95)
        return "雷雨";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86)
        return "雪";
    if (code >= 51 && code <= 82)
        return "雨";
    return "未知";
}
}  // namespace NetworkWeather
}  // namespace Crabit
