#include "diagnostic_service.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "comms_link.h"
#include "imu_telemetry.h"
#include "main.h"
#include "sensor_snapshot.h"
#include "topics.h"
#include "usb.h"

bool DiagnosticService::handleEarly(const CrabitWire::Message& message) {
    if (strcmp(message.topic, Topics::DiagBattery) == 0) {
        if (message.value_len != 4 || memcmp(message.value, "read", 4) != 0) {
            CrabitLink::send(message.topic, "invalid", message.request_id);
            return true;
        }
        StmSensorSnapshot snapshot{};
        const bool available = STM32_ReadSensorSnapshot(&snapshot);
        const auto& battery = snapshot.battery;
        const bool voltage_valid = available && battery.is_valid && battery.voltage_valid &&
            isfinite(battery.voltage) &&
            battery.voltage > 0.5f && battery.voltage < 6.0f;
        const bool percent_valid = available && battery.is_valid &&
            battery.percent >= 0 && battery.percent <= 100;
        const bool charge_valid = available && battery.is_valid && battery.charging_valid;
        // Keep the legacy unsigned wire format. The phone rejects valid=0 as a sample.
        char reply[128];
        snprintf(reply, sizeof(reply), "mv=%u;pct=%u;charging=%u;valid=%u;at=%lu;age=%lu",
            voltage_valid ? unsigned(battery.voltage * 1000.0f + 0.5f) : 0u,
            percent_valid ? unsigned(battery.percent) : 0u, (charge_valid && battery.is_charging) ? 1u : 0u,
            voltage_valid && percent_valid && charge_valid ? 1u : 0u, (unsigned long)battery.timestamp,
            (unsigned long)(uint32_t(HAL_GetTick() - battery.timestamp)));
        CrabitLink::send(message.topic, reply, message.request_id);
        return true;
    }
    if (strcmp(message.topic, Topics::AppUsb) == 0) {
        char reply[CrabitWire::MAX_VALUE + 1];
        Usb::scriptHandle(message.value, message.value_len, reply, sizeof(reply));
        CrabitLink::send(message.topic, reply, message.request_id);
        return true;
    }
    if (strcmp(message.topic, Topics::AppBadusb) == 0) {
        CrabitLink::send(message.topic, "UNSUPPORTED", message.request_id);
        return true;
    }
    return false;
}

void DiagnosticService::handleLate(const CrabitWire::Message& message) {
    if (strcmp(message.topic, Topics::DiagEcho) == 0) {
        CrabitLink::send(Topics::DiagEcho, message.value, message.request_id);
        return;
    }
    if (strcmp(message.topic, Topics::DiagImu) == 0) {
        StmSensorSnapshot snapshot{};
        char value[192];
        const bool available = STM32_ReadSensorSnapshot(&snapshot);
        if (Crabit::formatImuTilt(value, sizeof(value), snapshot.imu.ax,
                snapshot.imu.ay, snapshot.imu.az, available && snapshot.imu.is_valid,
                snapshot.imu.timestamp, HAL_GetTick())) {
            CrabitLink::send(Topics::DiagImu, value, message.request_id);
        }
        return;
    }
    if (strcmp(message.topic, Topics::DiagSnapshot) == 0) {
        StmSensorSnapshot snapshot{};
        char value[CrabitWire::MAX_VALUE + 1];
        if (STM32_ReadSensorSnapshot(&snapshot)) {
            const auto& battery = snapshot.battery;
            const int percent = battery.is_valid && battery.percent >= 0 && battery.percent <= 100
                ? battery.percent : -1;
            const int written = snprintf(value, sizeof(value),
                     "imu=%.3f,%.3f,%.3f;opt=%ld,%ld,%d;bmp=%.2f,%.2f;bat=%d;at=%lu;age=%lu",
                     (double)snapshot.imu.ax, (double)snapshot.imu.ay,
                     (double)snapshot.imu.az, (long)snapshot.optical.dx,
                     (long)snapshot.optical.dy, snapshot.optical.lifted ? 1 : 0,
                     (double)snapshot.pressure.pressure,
                     (double)snapshot.pressure.temperature,
                      percent, (unsigned long)snapshot.published_at,
                     (unsigned long)(uint32_t(HAL_GetTick() - snapshot.published_at)));
            if (written < 0 || (size_t)written >= sizeof(value)) {
                snprintf(value, sizeof(value), "unavailable;at=%lu;age=%lu",
                         (unsigned long)snapshot.published_at,
                         (unsigned long)(uint32_t(HAL_GetTick() - snapshot.published_at)));
            }
        } else {
            strncpy(value, "unavailable", sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';
        }
        CrabitLink::send(Topics::DiagSnapshot, value, message.request_id);
    }
}
