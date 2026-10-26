#pragma once
struct FakeGlobalStore { char ble_status[16]; };
extern FakeGlobalStore GlobalStore;
struct EventBus {
    static void publish(const char*, const char*);
    static bool tryPublish(const char*, const char*, uint16_t = 0);
};
