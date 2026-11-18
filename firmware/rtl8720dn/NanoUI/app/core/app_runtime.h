#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

// Minimal App runtime helpers shared by games and wireless Apps.
//
// Keep this header free of Wi-Fi, packet-injection and AuditRelay includes:
// game-only profiles need only these two helpers, so they must not compile the
// wireless stack just because Hopper paces frames with safe_delay(1).
#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>

extern volatile bool is_display_active;

inline void safe_delay(unsigned long ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

#endif
