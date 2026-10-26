#include "../../firmware/shared/imu_telemetry.h"
#include <cassert>
#include <cstring>
#include <limits>

int main() {
    char value[192];
    assert(Crabit::formatImuTilt(value, sizeof(value), 0, 0, 1, true, 100, 120));
    assert(std::strstr(value, "az=1.0000") && std::strstr(value, "age=20"));
    assert(std::strstr(value, "mode=tilt"));
    assert(Crabit::formatImuTilt(value, sizeof(value), 0, 0, 1, true, 0, 1001));
    assert(std::strstr(value, "valid=0"));
    assert(Crabit::formatImuTilt(value, sizeof(value), 0, 0, 1, false, 1, 2));
    assert(std::strstr(value, "unavailable"));
    assert(Crabit::formatImuTilt(value, sizeof(value),
        std::numeric_limits<float>::quiet_NaN(), 0, 1, true, 1, 2));
    assert(std::strstr(value, "valid=0"));
    assert(Crabit::formatImuTilt(value, sizeof(value), 0, 0, 1, true,
                               UINT32_MAX - 9, 10));
    assert(std::strstr(value, "age=20"));
    assert(!Crabit::formatImuTilt(value, 4, 0, 0, 1, true, 1, 2));
    assert(value[0] == '\0');
}
