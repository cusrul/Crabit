#pragma once
#include <cstdint>
struct RTC_TimeTypeDef {
    uint8_t Hours, Minutes, Seconds;
    uint32_t DayLightSaving = 0, StoreOperation = 0;
};
struct RTC_DateTypeDef {
    uint8_t Month, Date, WeekDay, Year;
};
inline int hrtc = 0;
constexpr int RTC_FORMAT_BIN = 0, HAL_OK = 0;
constexpr int RTC_DAYLIGHTSAVING_NONE = 0, RTC_STOREOPERATION_RESET = 0;
int HAL_RTC_GetTime(int*, RTC_TimeTypeDef*, int);
int HAL_RTC_GetDate(int*, RTC_DateTypeDef*, int);
int HAL_RTC_SetTime(int*, RTC_TimeTypeDef*, int);
int HAL_RTC_SetDate(int*, RTC_DateTypeDef*, int);
