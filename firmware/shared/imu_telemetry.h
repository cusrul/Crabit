#ifndef CRABIT_IMU_TELEMETRY_H
#define CRABIT_IMU_TELEMETRY_H

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstddef>

namespace Crabit {
inline void formatImuAxis(char (&out)[16], float value) {
    // newlib-nano is linked without _printf_float. Fixed-point formatting
    // avoids silently empty axes and does not pull in the float printf engine.
    const long scaled = static_cast<long>(std::fabs(value) * 10000.0f + 0.5f);
    std::snprintf(out, sizeof(out), "%s%ld.%04ld",
                  value < 0 ? "-" : "", scaled / 10000, scaled % 10000);
}
// Read-only telemetry. The current QMI driver exposes acceleration in g, not
// angular velocity. Never turn its zero-filled gyro fields into heading data.
inline bool formatImuTilt(char* out, size_t capacity, float ax, float ay,
                          float az, bool valid, uint32_t sampled_at,
                          uint32_t now) {
    if (!out || capacity == 0) return false;
    const uint32_t age = now - sampled_at;
    valid = valid && age <= 1000 && std::isfinite(ax) &&
        std::isfinite(ay) && std::isfinite(az) &&
        std::fabs(ax) <= 16 && std::fabs(ay) <= 16 && std::fabs(az) <= 16;
    char x[16]{}, y[16]{}, z[16]{};
    if (valid) {
        formatImuAxis(x, ax); formatImuAxis(y, ay); formatImuAxis(z, az);
    }
    const int count = valid
        ? std::snprintf(out, capacity,
            "ax=%s;ay=%s;az=%s;gx=0;gy=0;gz=0;at=%lu;age=%lu;valid=1;mode=tilt",
            x, y, z, static_cast<unsigned long>(sampled_at),
            static_cast<unsigned long>(age))
        : std::snprintf(out, capacity, "unavailable;valid=0;mode=tilt");
    if (count < 0 || static_cast<size_t>(count) >= capacity) {
        out[0] = '\0';
        return false;
    }
    return true;
}
} // namespace Crabit
#endif
