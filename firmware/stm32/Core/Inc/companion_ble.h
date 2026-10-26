#ifndef CRABIT_COMPANION_BLE_H
#define CRABIT_COMPANION_BLE_H

#include <stdint.h>
namespace Crabit { namespace CompanionBle {
// Requests, tick, messages and confirmation belong exclusively to the UI task.
bool start();
bool stop();
bool toggle();
// UI task: defer restoration until the BW16 UI FRAME app has fully unmounted.
void resumeAfterUi();
bool active();
bool transitioning();
const char* status();
const char* pairTransportStatus(); // Post-BLE UART check, not pairing/authentication.
uint16_t sessionId();
uint32_t pairSequence();
bool confirm(uint16_t session, uint32_t pair);
bool cancel(uint16_t session, uint32_t pair);
void tick();
void receive(uint16_t session, const char* value);
// Called only by the existing enable_ble boot branch, after its unchanged boot.
void registerLegacyAutostart();
bool uartProbeOnly();
bool legacyAutostartAllowed();
void observeProbePong(const char* value); // UI task, existing sys/notify subscription.
// UI task only: the proven FRAME startup transfers ownership only on ADVERTISING.
bool adoptModeBle();
void finishModeBleStart(const char* result); // After the FRAME app's off wait.
void observeModeStatus(const char* value); // Every sys/ble event, before cache overwrite.
// Atomics exported through these read-only helpers for the sensor task.
bool hardwareActive();
bool hardwareTransitioning();
} }
#endif
