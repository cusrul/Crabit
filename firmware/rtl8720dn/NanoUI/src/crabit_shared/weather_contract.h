#ifndef CRABIT_WEATHER_CONTRACT_H
#define CRABIT_WEATHER_CONTRACT_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

namespace CrabitWeather {
constexpr unsigned MaxDays = 7;
struct Place { int32_t latitude = 0, longitude = 0; int16_t offset = 480; uint8_t days = 3; };
struct Day { uint32_t date = 0; uint16_t code = 0; int16_t low = 0, high = 0; uint8_t rain = 0; };
inline bool numbers(const char* value, long* result, size_t count) {
    if (!value) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!(*value >= '0' && *value <= '9') && *value != '-') return false;
        errno = 0; char* end = nullptr;
        result[i] = strtol(value, &end, 10);
        if (errno || end == value || *end != (i + 1 == count ? '\0' : ',')) return false;
        value = end + (i + 1 != count);
    }
    return true;
}
inline bool leap(unsigned y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
inline unsigned monthDays(unsigned y, unsigned m) {
    const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return m >= 1 && m <= 12 ? days[m-1] + (m == 2 && leap(y)) : 0;
}
inline bool validDate(uint32_t date) {
    unsigned y = date / 10000, m = date / 100 % 100, d = date % 100;
    return y >= 2024 && y <= 2099 && d >= 1 && d <= monthDays(y, m);
}
inline bool parsePlace(const char* text, Place& out) {
    long v[4];
    if (!numbers(text, v, 4) || v[0] < -90000000L || v[0] > 90000000L ||
        v[1] < -180000000L || v[1] > 180000000L || v[2] < -720 || v[2] > 840 ||
        v[3] < 1 || v[3] > long(MaxDays)) return false;
    out.latitude = v[0]; out.longitude = v[1]; out.offset = v[2]; out.days = v[3];
    return true;
}
inline bool parseDay(const char* text, unsigned& index, Day& out) {
    long v[6];
    if (!numbers(text, v, 6) || v[0] < 0 || v[0] >= long(MaxDays) || v[1] < 0 ||
        !validDate(uint32_t(v[1])) || v[2] < 0 || v[2] > 99 ||
        v[3] < -1000 || v[4] > 1000 || v[3] > v[4] || v[5] < 0 || v[5] > 100) return false;
    index = v[0]; out.date = v[1]; out.code = v[2]; out.low = v[3]; out.high = v[4]; out.rain = v[5];
    return true;
}
inline bool unhex(const char* text, char* out, size_t capacity, bool empty = false) {
    size_t n = strlen(text);
    if (n % 2 || n / 2 >= capacity || (!empty && !n)) return false;
    for (size_t i = 0; i < n; i += 2) {
        unsigned byte = 0;
        for (size_t j = 0; j < 2; ++j) {
            const char c = text[i+j];
            int digit = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
            if (digit < 0) return false;
            byte = byte * 16 + digit;
        }
        if (!byte) return false;
        out[i/2] = char(byte);
    }
    out[n/2] = '\0'; return true;
}
struct Civil { uint16_t year; uint8_t month, day, hour, minute, second, weekday; };
inline bool civil(uint32_t utc, int16_t offset, Civil& out) {
    if (utc < 1704067200u || utc > 4102444799u || offset < -720 || offset > 840) return false;
    const int64_t local = int64_t(utc) + int64_t(offset) * 60;
    uint32_t days = uint32_t(local / 86400), remaining = days;
    unsigned y = 1970, m = 1;
    while (remaining >= (leap(y) ? 366u : 365u)) remaining -= leap(y++) ? 366 : 365;
    while (remaining >= monthDays(y, m)) remaining -= monthDays(y, m++);
    if (y < 2024 || y > 2099) return false;
    out.year = y; out.month = m; out.day = remaining + 1;
    out.hour = local / 3600 % 24; out.minute = local / 60 % 60; out.second = local % 60;
    out.weekday = (days + 3) % 7 + 1;
    return true;
}
}
#endif
