#ifndef CRABIT_COMPANION_BLE_H
#define CRABIT_COMPANION_BLE_H

#include <stdint.h>
namespace Crabit { namespace CompanionBle {
// Ports owned by the UI task and the board composition root. Plain C++ value
// types only: no HAL, NanoUI, App or pair-prompt dependency.
struct UiHooks {
    // Existing FRAME owner query; the same call already guards start/resume.
    bool (*frameAppActive)();
    // Equivalent to AppManager::launch(new CyberLinkApp(BleBackground)).
    void (*launchBleBackground)();
    // Existing Popup::showToast text, unmodified by this port.
    void (*showToast)(const char* message);
    // Existing CompanionPairPrompt::dismiss().
    void (*dismissPairPrompt)();
    // Existing CompanionPairPrompt::invalidate(session, pair).
    void (*invalidatePairPrompt)(uint16_t session, uint32_t pair);
    // Existing CompanionPairPrompt::tick(session, pair).
    void (*tickPairPrompt)(uint16_t session, uint32_t pair);
};

struct HardwareHooks {
    // Existing BW16_NormalBoot() body, injected by the board.
    void (*normalBoot)();
    // Existing BW16_ForceOff() body, injected by the board.
    void (*forceOff)();
};

// UI task boot, once: installs both port sets. Rejects a missing callback and
// any second bind without partially updating, and never resets session state.
// Before a successful bind the owner-driving entry points stay inert: start,
// stop and toggle report false, resumeAfterUi, tick and receive do nothing, so
// no boot/success is faked and no hardware or UI callback runs. The read-only
// snapshots keep their defaults, and the boot-time legacy tracker
// (registerLegacyAutostart) plus the pure status observers remain available.
bool bind(const UiHooks& ui, const HardwareHooks& hardware);
bool bound();

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
