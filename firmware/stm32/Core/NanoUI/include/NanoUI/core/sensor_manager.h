// 路径：NanoUI/src/core/sensor_manager.h
#ifndef NANOUI_SENSOR_MANAGER_H
#define NANOUI_SENSOR_MANAGER_H

#include <stdint.h>
#include "embedded_vector.h"

namespace NanoUI {

// 纯粹的传感器抽象基类
class Sensor {
private:
    uint32_t interval_ms;  
    uint32_t last_tick;    

public:
    Sensor(uint32_t interval) : interval_ms(interval), last_tick(0) {}
    virtual ~Sensor() {}

    virtual void init() = 0;
    virtual void update() = 0;
    virtual const char* getName() = 0;
    
    // 核心魔法：使用 const void* 返回数据指针。
    // 框架不关心里面存的是什么，只有具体的 Driver 和 App 知道怎么解析它。
    virtual const void* getData() = 0; 

    bool checkTick(uint32_t now) {
        if (now - last_tick >= interval_ms) {
            last_tick = now;
            return true;
        }
        return false;
    }
};

// 瞎子调度器：只管按时点名
class SensorManager {
private:
    NanoUI::EmbeddedVector<Sensor*> sensors;
    SensorManager() {} 

public:
    static SensorManager& getInstance() {
        static SensorManager instance;
        return instance;
    }

    void registerSensor(Sensor* s) {
        sensors.push_back(s);
        s->init();
    }

    void tick(uint32_t now_ms) {
        for (auto sensor : sensors) {
            if (sensor->checkTick(now_ms)) {
                sensor->update(); 
            }
        }
    }

    Sensor* getSensor(const char* name) {
        for (auto sensor : sensors) {
            const char* sn = sensor->getName();
            const char* rn = name;
            while (*sn && *rn && *sn == *rn) { sn++; rn++; }
            if (*sn == 0 && *rn == 0) return sensor;
        }
        return nullptr;
    }
};

} // namespace NanoUI
#endif
