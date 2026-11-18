#pragma once

class WindowManager;

// Product service wiring: system notifications, BLE status mirroring, RTC
// time sync and the BadUSB armory. Kept out of the NanoUI port so service
// changes never require reading the main loop.
class AppServices {
public:
    static void init(WindowManager* wm);
};
