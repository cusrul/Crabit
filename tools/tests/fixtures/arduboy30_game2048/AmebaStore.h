#pragma once
#include <stdint.h>
namespace AmebaStore {
extern uint8_t raw_btn_mask;
extern float imu_ax, imu_ay, imu_az;
extern uint32_t imu_updated_at;
extern bool imu_received;
}
