#ifndef NANOUI_DATA_HUB_H
#define NANOUI_DATA_HUB_H

#include <stdint.h>
#include <string.h>

namespace NanoUI {

struct SystemData {
    uint32_t uptimeMs;
    uint32_t freeHeap;
    uint32_t totalHeap;
    struct {
        int percent;
        float voltage;
        bool is_valid;
    } battery;
};

class DataHub {
public:
    static SystemData shared;
};

}

#endif
