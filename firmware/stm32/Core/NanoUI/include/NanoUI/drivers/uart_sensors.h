#ifndef UART_SENSORS_H
#define UART_SENSORS_H

#include "../src/core/sensor_manager.h"
#include "../src/core/sensor_types.h"

extern volatile int8_t g_virtual_opt_dx;
extern volatile int8_t g_virtual_opt_dy;
extern volatile bool   g_virtual_opt_lifted;

class UartPAT9125Driver : public NanoUI::Sensor {
private:
    NanoUI::SensorDataOptical data_pack;
    
public:
    // 10ms 刷新一次传感器
    UartPAT9125Driver() : Sensor(10) {
        data_pack = {0, 0, 0, 0, 0, 0, 0, 0, 0.0f, true, 0};
    }
    
    const char* getName() override { return "PAT9125"; }
    
    void init() override {}

    void update() override {
        // 把 STM32 发来的增量合并到当前坐标里
        data_pack.dx = g_virtual_opt_dx;
        data_pack.dy = g_virtual_opt_dy;
        
        if (!g_virtual_opt_lifted) {
            data_pack.x += data_pack.dx;
            data_pack.y += data_pack.dy;
        }
        
        data_pack.lifted = g_virtual_opt_lifted;

        // 消费完毕，清零等待下一波 UART 数据
        g_virtual_opt_dx = 0;
        g_virtual_opt_dy = 0;
    }
    
    const void* getData() override { return &data_pack; }
};

#endif