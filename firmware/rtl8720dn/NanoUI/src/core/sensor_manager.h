#ifndef NANOUI_SENSOR_MANAGER_H
#define NANOUI_SENSOR_MANAGER_H

#include <stdint.h>
#include <vector>
#include "sensor_types.h"

namespace NanoUI {

class Sensor {
private:
    uint32_t interval_ms;
    uint32_t last_ms = 0;
public:
    Sensor(uint32_t interval) : interval_ms(interval) {}
    virtual ~Sensor() {}
    virtual void init() = 0;
    virtual void update() = 0;
    virtual const void* getData() = 0;
    virtual const char* getName() = 0;
    bool checkTick(uint32_t now) {
        if (now - last_ms >= interval_ms) { last_ms = now; return true; }
        return false;
    }
};

class SensorManager {
private:
    std::vector<Sensor*> sensors;
    SensorManager() {} 
public:
    static SensorManager& getInstance() { static SensorManager instance; return instance; }
    void registerSensor(Sensor* s) { sensors.push_back(s); s->init(); }
    void tick(uint32_t now_ms) {
        for (auto s : sensors) { if (s->checkTick(now_ms)) s->update(); }
    }
    Sensor* getSensor(const char* name) {
        for (auto s : sensors) {
            const char* sn = s->getName(); const char* rn = name;
            while (*sn && *rn && *sn == *rn) { sn++; rn++; }
            if (*sn == 0 && *rn == 0) return s;
        }
        return nullptr;
    }
};

} 
#endif
