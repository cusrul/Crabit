#ifndef CRABIT_NANOUI_SIM_COMPANION_BLE_H
#define CRABIT_NANOUI_SIM_COMPANION_BLE_H

#include <stdint.h>

namespace Crabit { namespace CompanionBle {
inline bool& simActive() { static bool active = false; return active; }
inline bool start() { simActive() = true; return true; }
inline bool stop() { simActive() = false; return true; }
inline bool toggle() { simActive() = !simActive(); return true; }
inline void resumeAfterUi() {}
inline bool active() { return simActive(); }
inline bool transitioning() { return false; }
inline const char* status() { return active() ? "SIMULATED" : "OFF"; }
inline const char* pairTransportStatus() { return "DEMO ONLY"; }
inline uint16_t sessionId() { return 0; }
inline uint32_t pairSequence() { return 0; }
inline bool confirm(uint16_t, uint32_t) { return false; }
inline bool cancel(uint16_t, uint32_t) { return false; }
inline void tick() {}
inline void receive(uint16_t, const char*) {}
inline void registerLegacyAutostart() {}
inline bool uartProbeOnly() { return false; }
inline bool legacyAutostartAllowed() { return false; }
inline void observeProbePong(const char*) {}
inline bool adoptModeBle() { return false; }
inline void finishModeBleStart(const char*) {}
inline void observeModeStatus(const char*) {}
inline bool hardwareActive() { return false; }
inline bool hardwareTransitioning() { return false; }
} }
#endif
