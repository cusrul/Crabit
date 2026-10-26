#ifndef CRABIT_NANOUI_SIM_GLOBAL_STORE_H
#define CRABIT_NANOUI_SIM_GLOBAL_STORE_H

#include <stdint.h>
#include "product_preferences.h"

struct GlobalDataStore {
    bool wifi_connected = false;
    int ble_client_count = 0;
    char latest_notify[64] = "模拟运行";
    char ble_status[16] = "OFF";
    char wifi_status[16] = "OFF";
    uint8_t sys_volume = 100;
};
inline GlobalDataStore GlobalStore{};

#endif
