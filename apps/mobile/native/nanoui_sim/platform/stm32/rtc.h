#ifndef CRABIT_NANOUI_SIM_RTC_H
#define CRABIT_NANOUI_SIM_RTC_H

#include <stdint.h>
#include "main.h"
#include <ctime>

struct RTC_HandleTypeDef {};
struct RTC_TimeTypeDef {
    uint8_t Hours = 0, Minutes = 0, Seconds = 0;
    uint32_t DayLightSaving = 0, StoreOperation = 0;
};
struct RTC_DateTypeDef { uint8_t Year = 26, Month = 10, Date = 25, WeekDay = 7; };
inline RTC_HandleTypeDef hrtc{};
constexpr uint32_t RTC_FORMAT_BIN = 0;
constexpr uint32_t RTC_DAYLIGHTSAVING_NONE = 0;
constexpr uint32_t RTC_STOREOPERATION_RESET = 0;

inline RTC_TimeTypeDef& crabit_sim_rtc_time() {
    static RTC_TimeTypeDef value{};
    return value;
}
inline RTC_DateTypeDef& crabit_sim_rtc_date() {
    static RTC_DateTypeDef value{};
    return value;
}
inline uint32_t& crabit_sim_clock_epoch() {
    static uint32_t value = 1792886400u;
    return value;
}
inline uint32_t& crabit_sim_clock_tick() {
    static uint32_t value = 0;
    return value;
}
inline bool& crabit_sim_clock_set() {
    static bool value = false;
    return value;
}
inline uint32_t crabit_sim_rtc_epoch_value() {
    std::tm tm{};
    tm.tm_year = 100 + crabit_sim_rtc_date().Year;
    tm.tm_mon = crabit_sim_rtc_date().Month - 1;
    tm.tm_mday = crabit_sim_rtc_date().Date;
    tm.tm_hour = crabit_sim_rtc_time().Hours;
    tm.tm_min = crabit_sim_rtc_time().Minutes;
    tm.tm_sec = crabit_sim_rtc_time().Seconds;
#if defined(_WIN32)
    return static_cast<uint32_t>(_mkgmtime(&tm));
#else
    return static_cast<uint32_t>(timegm(&tm));
#endif
}
inline void crabit_sim_rtc_assign_fields(uint32_t epoch) {
    const std::time_t raw = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &raw);
#else
    gmtime_r(&raw, &tm);
#endif
    crabit_sim_rtc_time().Hours = static_cast<uint8_t>(tm.tm_hour);
    crabit_sim_rtc_time().Minutes = static_cast<uint8_t>(tm.tm_min);
    crabit_sim_rtc_time().Seconds = static_cast<uint8_t>(tm.tm_sec);
    crabit_sim_rtc_date().Year = static_cast<uint8_t>(tm.tm_year - 100);
    crabit_sim_rtc_date().Month = static_cast<uint8_t>(tm.tm_mon + 1);
    crabit_sim_rtc_date().Date = static_cast<uint8_t>(tm.tm_mday);
    crabit_sim_rtc_date().WeekDay = static_cast<uint8_t>(tm.tm_wday);
}
inline void crabit_sim_rtc_refresh() {
    if (!crabit_sim_clock_set()) return;
    const uint32_t elapsed = static_cast<uint32_t>(hal_get_ticks() - crabit_sim_clock_tick()) / 1000u;
    crabit_sim_rtc_assign_fields(crabit_sim_clock_epoch() + elapsed);
}
inline HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef*, RTC_TimeTypeDef* out, uint32_t) {
    if (!out) return 1;
    crabit_sim_rtc_refresh();
    *out = crabit_sim_rtc_time();
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef*, RTC_DateTypeDef* out, uint32_t) {
    if (!out) return 1;
    crabit_sim_rtc_refresh();
    *out = crabit_sim_rtc_date();
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef*, RTC_TimeTypeDef* value, uint32_t) {
    if (!value) return 1;
    crabit_sim_rtc_time() = *value;
    crabit_sim_clock_set() = true;
    crabit_sim_clock_epoch() = crabit_sim_rtc_epoch_value();
    crabit_sim_clock_tick() = hal_get_ticks();
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef*, RTC_DateTypeDef* value, uint32_t) {
    if (!value) return 1;
    crabit_sim_rtc_date() = *value;
    crabit_sim_clock_set() = true;
    crabit_sim_clock_epoch() = crabit_sim_rtc_epoch_value();
    crabit_sim_clock_tick() = hal_get_ticks();
    return HAL_OK;
}

#endif
