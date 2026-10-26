#ifndef CRABIT_NANOUI_SIM_NETWORK_WEATHER_H
#define CRABIT_NANOUI_SIM_NETWORK_WEATHER_H

#include "weather_contract.h"
#include <ctime>

namespace Crabit { namespace NetworkWeather {
inline CrabitWeather::Day& simDay() {
    static CrabitWeather::Day day{};
    return day;
}
inline uint32_t& simFetchedAt() {
    static uint32_t value = 0;
    return value;
}
inline bool& simHasWeather() {
    static bool value = false;
    return value;
}
inline unsigned phoneCount() { return simHasWeather() ? 1u : 0u; }
inline const CrabitWeather::Day& phoneDay(unsigned) { return simDay(); }
inline const char* condition(uint16_t code) {
    if (code == 0) return "晴";
    if (code <= 3) return "多云";
    if (code == 45 || code == 48) return "雾";
    if (code >= 95) return "雷雨";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "雪";
    if (code >= 51 && code <= 82) return "雨";
    return "未知";
}
inline const char* phoneLabel() { return simHasWeather() ? "预览" : "未获取"; }
inline uint32_t lastUpdated() { return simFetchedAt(); }
inline int16_t lastUpdatedOffset() { return 0; }
inline void setPreviewWeather(uint32_t date, uint16_t code, int16_t low, int16_t high,
                              uint8_t rain, uint32_t fetched) {
    simDay() = {date, code, low, high, rain};
    simFetchedAt() = fetched;
    simHasWeather() = true;
}
inline void clearPreviewWeather() {
    simDay() = {};
    simFetchedAt() = 0;
    simHasWeather() = false;
}
} }

#endif
