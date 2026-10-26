#ifndef UART_SENSORS_H
#define UART_SENSORS_H

#include "../core/sensor_manager.h"
#include "../core/sensor_types.h"
#include "../../AmebaStore.h" 

class UartPAT9125Driver : public NanoUI::Sensor {
private:
    NanoUI::SensorDataOptical data_pack;
public:
    UartPAT9125Driver() : Sensor(10) {
        memset(&data_pack, 0, sizeof(data_pack));
        data_pack.is_valid = true;
    }

    const char* getName() override { return "PAT9125"; }
    
    void init() override {}

    void update() override {
        data_pack.dx = AmebaStore::opt_dx;
        data_pack.dy = AmebaStore::opt_dy;
        data_pack.lifted = AmebaStore::opt_lifted;
        AmebaStore::opt_dx = 0;
        AmebaStore::opt_dy = 0;
    }
    
    const void* getData() override { return &data_pack; }
};

#endif
