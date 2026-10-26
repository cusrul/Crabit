// Compile/link measurement only. Never called by setup, loop, a menu or UART.
#if defined(CRABIT_MEASURE_EXTERNAL_BLE_SPAM) && CRABIT_MEASURE_EXTERNAL_BLE_SPAM
#include <BW16BLESpam.h>

// The measurement wrapper retains this function with --undefined. This keeps
// public library paths from being garbage-collected without running any radio
// operation. Do not expose this probe as a product/runtime entry point.
extern "C" void crabit_ble_spam_size_probe(BW16BLESpammer& spammer) {
    spammer.begin();
    spammer.setDebugLogging(false);
    spammer.setInterval(1000);
    spammer.start(BW16BLESpammer::MICROSOFT);
    spammer.tick();
    (void)spammer.isActive();
    spammer.stop();
}
#endif
