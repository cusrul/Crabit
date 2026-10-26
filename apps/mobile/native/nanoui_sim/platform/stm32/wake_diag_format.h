#ifndef CRABIT_NANOUI_SIM_WAKE_DIAG_FORMAT_H
#define CRABIT_NANOUI_SIM_WAKE_DIAG_FORMAT_H
#include <cstdio>
#include <cstddef>
#include <cstdint>
namespace WakeDiag {
enum class Row { Samples };
inline void format(char* out, size_t size, Row, bool imu, bool pat, uint32_t clock) {
    std::snprintf(out, size, "%s/%s %luM", imu ? "IMU" : "--", pat ? "PAT" : "--",
                  static_cast<unsigned long>(clock / 1000000u));
}
}
#endif
