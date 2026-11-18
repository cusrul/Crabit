#pragma once

#include <stddef.h>
#include <stdint.h>

// Platform device facade. Upper layers include this header only; the USB
// device stack, CDC/HID classes and their headers stay below it.
#ifdef __cplusplus
class Usb {
public:
    // Device lifecycle
    static bool init();
    static bool initLog();
    static bool suspend();
    static bool resume();
    static bool active();
    static bool configured();
    static uint8_t state();

    // Console
    static void log(const char* format, ...);

    // Diagnostics / script channel
    static void diagTick();
    static void scriptHandle(const char* command, size_t length, char* reply, size_t capacity);

    // Raw CDC path used by the BW16 flash bridge. Normal writers are refused
    // while the bridge owns the CDC IN endpoint.
    static constexpr uint16_t MaxTransfer = 1024;
    static bool transmit(const uint8_t* data, uint16_t length);
    // Bridge-only bypass of the normal-writer gate. Callers must already hold
    // the exclusive UART owner gate in FLASH mode.
    static bool transmitBridge(const uint8_t* data, uint16_t length);
    static bool rxReady();
    static const uint8_t* rxData();
    static uint32_t rxLength();
    static void clearRx();
    static void consumeRx();
    static void resumeReceive();
};

#define USB_LOG(...) Usb::log(__VA_ARGS__)

extern "C" {
#endif

// C entry point for the CubeMX startup task.
void Usb_Init(void);

#ifdef __cplusplus
}
#endif
