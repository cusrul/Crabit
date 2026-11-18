#ifndef HW_SENSORS_H
#define HW_SENSORS_H

#include "NanoUI/core/sensor_manager.h"
#include "NanoUI/core/sensor_types.h"
#include "i2c_bus.h"
#include "axp2101.h"
#include "i2c_bus.h"
#include "power_policy.h"
#include "qmi_run.h"
#include "ameba_session.h"
#include "game_tone_service.h"
#include "../../../shared/product_preferences.h"

#if __has_include("adc.h")
#include "adc.h"
#endif


extern volatile uint32_t g_qmi_int1_cnt; // PB1
extern volatile uint32_t g_qmi_int2_cnt; // PB0
// Sensor-task classified Tap events, not raw EXTI edges. Aligned 32-bit reads
// are atomic on this MCU; the task publishes the timestamp before the count.
extern volatile uint32_t g_qmi_tap_cnt;
extern volatile uint32_t g_qmi_tap_tick;
extern volatile uint32_t g_pat_int_cnt;  // PA8
// Existing OLED-handover ownership flag; the game-tone gate reuses it as the
// trustworthy "BW16/coproc owns the product" signal.
extern volatile bool g_coproc_active;

// ==========================================
// 1. QMI8658 惯性传感器驱动 (基于原版最小修改)
// ==========================================
class QMI8658_Driver : public NanoUI::Sensor {
private:
    NanoUI::SensorDataIMU data_pack; 
    uint8_t addr = 0;
    bool tap_ready = false;
    bool tap_reported = false;

    bool writeRegister(uint8_t reg, uint8_t value) {
        return I2cBus::write(addr, reg, &value, 1, 10);
    }

    bool writeChecked(uint8_t reg, uint8_t value) {
        uint8_t actual = 0;
        return writeRegister(reg, value) &&
            I2cBus::read(addr, reg, &actual, 1, 10) && actual == value;
    }

    bool execute_ctrl9(uint8_t cmd) {
        uint8_t status = 0;
        if (!writeRegister(0x0A, cmd)) return false;
        bool done = false;
        for (int i = 0; i < 50; i++) {
            if (!I2cBus::read(addr, 0x2D, &status, 1, 2)) break;
            if (status & 0x80) { done = true; break; }
            HAL_Delay(1);
        }
        if (!writeRegister(0x0A, 0)) return false;
        if (!done) return false;
        for (int i = 0; i < 50; i++) {
            if (!I2cBus::read(addr, 0x2D, &status, 1, 2)) return false;
            if ((status & 0x80) == 0) return true;
            HAL_Delay(1);
        }
        return false;
    }

    bool configureTap() {
        // QST QMI8658A/C Rev A, sections 5.10 and 10. Disable sensors before
        // CAL/CTRL9; route command completion to STATUSINT.bit7 first.
        if (!writeRegister(0x08, 0) || !writeRegister(0x09, 0xC0)) return false;
        // 8g/250Hz; host polling stays 50Hz, axis scaling remains 4096/g.
        if (!writeChecked(0x03, QmiRunProfile::Ctrl2)) return false;
        auto timing = QmiRunProfile::TapTiming;
        auto threshold = QmiRunProfile::TapThreshold;
        if (!I2cBus::write(addr, 0x0B, timing.data(), 8, 20) ||
            !execute_ctrl9(0x0C) ||
            !I2cBus::write(addr, 0x0B, threshold.data(), 8, 20) ||
            !execute_ctrl9(0x0C)) return false;
        if (!writeRegister(0x09, 0xC1)) return false; // Tap engine, INT1 routing.
        uint8_t readback = 0;
        return I2cBus::read(addr, 0x09, &readback, 1, 10) &&
            readback == 0xC1;
    }

public:
    QMI8658_Driver() : Sensor(20) { data_pack = {}; } 

    const char* getName() override { return "QMI8658"; }
    uint16_t busAddress() const { return addr; }
    bool tapReady() const { return tap_ready; }
    
    void init() override {
        addr = 0;
        tap_ready = false;
        tap_reported = false;
        data_pack.is_valid = false;
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
        GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
        GPIO_InitStruct.Pull = GPIO_PULLDOWN; 
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        uint8_t id = 0, val = 0;
        if (I2cBus::read(0xD4, 0x00, &id, 1, 10) && id == 0x05) addr = 0xD4;
        else if (I2cBus::read(0xD6, 0x00, &id, 1, 10) && id == 0x05) addr = 0xD6;

        if (addr != 0) {
            val = 0xB0; I2cBus::write(addr, 0x60, &val, 1, 10); // 软复位
            HAL_Delay(50);
            
            // Preserve the existing address-increment and INT pin configuration.
            val = 0x5E; I2cBus::write(addr, 0x02, &val, 1, 10); // CTRL1: 开INT1/2，地址自增
            
            tap_ready = configureTap();
            if (!tap_ready) writeRegister(0x09, 0xC0); // Fail closed for Tap only.
            // Keep acceleration available even if the optional Tap setup fails.
            const bool accel_ready = writeChecked(0x03, QmiRunProfile::Ctrl2) && writeChecked(0x08, 0x21);
            tap_ready = tap_ready && accel_ready;
        }
    }

    // 进入低功耗前写入降速档位。
    void sleepForWakeOnMotion() {
        if (addr == 0) return;
        uint8_t val;
        // Legacy deep-sleep path; not used by the screen-only wake candidate.
        val = 0x2F; I2cBus::write(addr, 0x03, &val, 1, 10); 
    }

    // 唤醒后恢复运行态档位。
    void wakeUp() {
        if (addr == 0) return;
        uint8_t val;
        // Legacy facade: keep the running profile consistent with init().
        val = QmiRunProfile::Ctrl2; I2cBus::write(addr, 0x03, &val, 1, 10);
    }
    
    void update() override {
        if (SensorStop2::restoreFault.load(std::memory_order_relaxed)) {
            data_pack.is_valid = false; return;
        }
        if (addr == 0) return;
        if (I2cFault::paused.load(std::memory_order_relaxed)) return;
        I2cFault::Sample trace(I2cFault::Qmi, addr);
        uint8_t status1 = 0;
        if (tap_ready && I2cBus::read(addr, 0x2F, &status1, 1, 10)) {
            if ((status1 & 0x02) == 0) tap_reported = false;
            else {
                uint8_t tap_status = 0;
                if (I2cBus::read(addr, 0x59, &tap_status, 1, 10)) {
                    const uint8_t number = tap_status & 3;
                    const bool tap = (number == 1 || number == 2) && (tap_status & 0x30) != 0;
                    if (tap && !tap_reported) {
                        g_qmi_tap_tick = HAL_GetTick();
                        ++g_qmi_tap_cnt;
                    }
                    // A latched status must never create a wake storm.
                    tap_reported = true;
                }
            }
        }

        uint8_t data[6] = {0};
        if (I2cBus::read(addr, 0x35, data, 6, 20)) {
            // The QMI8658 +X axis points to the watch's left edge as mounted on
            // this PCB. Convert once here so every consumer (UI tilt, BW16
            // telemetry, games, BLE mouse) shares the same convention:
            // +ax = right edge down, +ay = bottom edge down, +az = screen up.
            data_pack.ax = -(int16_t)((data[1] << 8) | data[0]) / 4096.0f;
            data_pack.ay = (int16_t)((data[3] << 8) | data[2]) / 4096.0f;
            data_pack.az = (int16_t)((data[5] << 8) | data[4]) / 4096.0f;
            data_pack.gx = 0; data_pack.gy = 0; data_pack.gz = 0; 
            data_pack.int1_count = g_qmi_int1_cnt;
            data_pack.int2_count = g_qmi_int2_cnt;
            data_pack.is_valid = true;
            data_pack.timestamp = HAL_GetTick();
        } else {
            data_pack.is_valid = false;
        }
    }
    
    const void* getData() override { return &data_pack; }
};

// ==========================================
// 2. 电量兼容传感器
// 逻辑名仍然叫 BATTERY，方便旧 UI 继续读取；
// 底层数据来源已经完全切到 AXP2101。
// ==========================================
class Battery_Driver : public NanoUI::Sensor {
private:
    NanoUI::SensorDataBattery data_pack;
public:
    Battery_Driver() : Sensor(1000) { data_pack = {}; }
    const char* getName() override { return "BATTERY"; }
    void init() override {
        data_pack = {};
        data_pack.percent = -1;  // Unknown until the first successful gauge read.
        data_pack.is_valid = false;
    }
    void update() override {
        if (I2cFault::paused.load(std::memory_order_relaxed)) return;
        if (AuxStop2::restoreFault.load(std::memory_order_relaxed)) {
            data_pack.is_valid = false; return;
        }
        I2cFault::Sample trace(I2cFault::Battery, AXP2101_ADDR);

        // Each field has its own validity: a failed gauge read must not hide a
        // good voltage/charge sample, and vice versa. Unknown values are
        // published as explicit sentinels, never as a plausible 0%.
        uint16_t millivolts = 0;
        const bool voltage_ok = AXP2101_TryGetBattVoltage(&millivolts) && millivolts > 500;
        uint8_t percent = 0;
        const bool percent_ok = AXP2101_TryGetBatteryPercent(&percent);
        bool charging = false;
        const bool charging_ok = AXP2101_TryGetCharging(&charging);

        if (voltage_ok) {
            const float v = millivolts / 1000.0f;
            if (data_pack.voltage < 1.0f) data_pack.voltage = v;
            else data_pack.voltage = data_pack.voltage * 0.9f + v * 0.1f;
        } else {
            data_pack.voltage = 0.0f;
        }
        data_pack.voltage_valid = voltage_ok;
        data_pack.percent = percent_ok ? (int)percent : -1;
        data_pack.is_charging = charging_ok && charging;
        data_pack.charging_valid = charging_ok;
        data_pack.timestamp = HAL_GetTick();
        data_pack.is_valid = voltage_ok || percent_ok || charging_ok;
    }
    const void* getData() override { return &data_pack; }
};

// ==========================================
// 3. PAT9125 光学传感器驱动 (保持不变)
// ==========================================
class PAT9125_Driver : public NanoUI::Sensor {
private:
    NanoUI::SensorDataOptical data_pack; 
    uint8_t addr = 0;
public:
    PAT9125_Driver() : Sensor(15) { 
        data_pack = {}; 
        data_pack.agc = 4.0f;
        data_pack.lifted = true;
    }
    const char* getName() override { return "PAT9125"; }
    uint16_t busAddress() const { return addr; }

    void init() override {
        uint8_t id = 0, val = 0;
        if (I2cBus::read((0x75<<1), 0x00, &id, 1, 10)) addr = (0x75<<1);
        else if (I2cBus::read((0x79<<1), 0x00, &id, 1, 10)) addr = (0x79<<1);

        if (addr != 0) {
            val = 0x5A; I2cBus::write(addr, 0x09, &val, 1, 10); 
            val = 0x00; I2cBus::write(addr, 0x0B, &val, 1, 10); 
            val = 0x00; I2cBus::write(addr, 0x0A, &val, 1, 10); 
            // 将 Tap 灵敏度相关寄存器设为 0xFF，当前为最大值。
            val = 0xFF; I2cBus::write(addr, 0x0D, &val, 1, 10); 
            val = 0xFF; I2cBus::write(addr, 0x0E, &val, 1, 10);
        }
    }

    void update() override {
        if (SensorStop2::restoreFault.load(std::memory_order_relaxed)) {
            data_pack.is_valid = false; return;
        }
        if (addr == 0) { data_pack.is_valid = false; return; }
        if (I2cFault::paused.load(std::memory_order_relaxed)) return;
        I2cFault::Sample trace(I2cFault::Pat, addr);
        uint8_t reg_data[3]; 
        
        if (I2cBus::read(addr, 0x02, reg_data, 3, 10)) {
            uint8_t shutter = 0, frame = 0;
            I2cBus::read(addr, 0x14, &shutter, 1, 10);
            I2cBus::read(addr, 0x17, &frame, 1, 10);
            
            data_pack.mot  = reg_data[0];
            data_pack.dx   = (int8_t)reg_data[1];
            data_pack.dy   = (int8_t)reg_data[2];
            data_pack.mode = shutter; 
            data_pack.agc  = (float)frame; 

            data_pack.lifted = (shutter >= 11) || (shutter <= 3);

            if ((data_pack.mot & 0x80) && !data_pack.lifted) {
                data_pack.x += data_pack.dx;
                data_pack.y += data_pack.dy;
            }

            data_pack.int_count = g_pat_int_cnt;
            data_pack.is_valid = true;
            data_pack.timestamp = HAL_GetTick();
        } else {
            data_pack.is_valid = false;
        }
    }
    
    void sleep(bool enable) {
        uint8_t data = enable ? 0x06 : 0x00;
        I2cBus::write(addr, 0x0B, &data, 1, 100);
    }

    const void* getData() override { return &data_pack; }
};

// ==========================================
// 4b. AXP2101 PMU 复合驱动
// ==========================================
class AXP2101_Driver : public NanoUI::Sensor {
private:
    NanoUI::SensorDataAXP2101 data_pack;
    uint8_t invalid_streak = 0;
public:
    AXP2101_Driver() : Sensor(1000) { data_pack = {}; }
    const char* getName() override { return "AXP2101"; }
    void init() override {
        data_pack = {};
        data_pack.percent = -1;  // Unknown until the first successful gauge read.
        invalid_streak = 0;
        // Board startup owns PMU configuration. Do not touch hardware here.
        data_pack.is_valid = false;
    }
    void update() override {
        if (I2cFault::paused.load(std::memory_order_relaxed)) return;
        if (AuxStop2::restoreFault.load(std::memory_order_relaxed)) {
            data_pack.is_valid = false; return;
        }
        I2cFault::Sample trace(I2cFault::Axp, AXP2101_ADDR);
        uint8_t chip_id = 0;
        if (AXP2101_ReadReg(AXP2101_IC_TYPE, &chip_id) != HAL_OK || chip_id != 0x4A) {
            if (invalid_streak < 3 && data_pack.is_valid) {
                invalid_streak++;
                return;
            }
            data_pack.is_valid = false;
            data_pack.voltage_valid = false;
            data_pack.charging_valid = false;
            data_pack.percent = -1;
            return;
        }

        // Publish each measurement with its own validity; only the PMU
        // presence check above may invalidate the whole packet.
        uint16_t millivolts = 0;
        const bool voltage_ok = AXP2101_TryGetBattVoltage(&millivolts);
        uint8_t percent = 0;
        const bool percent_ok = AXP2101_TryGetBatteryPercent(&percent);
        bool is_charging = false;
        const bool charging_ok = AXP2101_TryGetCharging(&is_charging);
        bool is_vbus_in = AXP2101_IsVbusIn();
        float temp = AXP2101_GetInternalTemp();
        float v_dcdc3 = AXP2101_GetDC3Voltage() / 1000.0f;

        data_pack.voltage = voltage_ok ? millivolts / 1000.0f : 0.0f;
        data_pack.voltage_valid = voltage_ok;
        data_pack.percent = percent_ok ? (int)percent : -1;
        data_pack.current = 0;
        data_pack.is_charging = charging_ok && is_charging;
        data_pack.charging_valid = charging_ok;
        data_pack.is_vbus_in = is_vbus_in;
        data_pack.temp = temp;
        data_pack.v_dcdc1 = 0.0f;
        data_pack.v_dcdc2 = 0.0f;
        data_pack.v_dcdc3 = v_dcdc3;
        data_pack.v_aldo1 = 0.0f;
        data_pack.v_aldo2 = 0.0f;
        data_pack.v_aldo3 = 0.0f;
        data_pack.timestamp = HAL_GetTick();
        data_pack.is_valid = true;
        invalid_streak = 0;
    }
    const void* getData() override { return &data_pack; }
};

// ==========================================
// 4c. BMP280 气压传感器驱动
// ==========================================
class BMP280_Driver : public NanoUI::Sensor {
private:
    NanoUI::SensorDataPress data_pack;
    uint8_t addr = 0xEC;
    struct {
        uint16_t dig_T1;
        int16_t dig_T2;
        int16_t dig_T3;
        uint16_t dig_P1;
        int16_t dig_P2;
        int16_t dig_P3;
        int16_t dig_P4;
        int16_t dig_P5;
        int16_t dig_P6;
        int16_t dig_P7;
        int16_t dig_P8;
        int16_t dig_P9;
    } cal = {};
    int32_t t_fine = 0;

    static uint16_t U16LE(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    static int16_t S16LE(const uint8_t* p) { return (int16_t)U16LE(p); }

    bool readCalibration() {
        uint8_t buf[24] = {0};
        if (!I2cBus::read(addr, 0x88, buf, sizeof(buf), 20)) return false;
        cal.dig_T1 = U16LE(&buf[0]);
        cal.dig_T2 = S16LE(&buf[2]);
        cal.dig_T3 = S16LE(&buf[4]);
        cal.dig_P1 = U16LE(&buf[6]);
        cal.dig_P2 = S16LE(&buf[8]);
        cal.dig_P3 = S16LE(&buf[10]);
        cal.dig_P4 = S16LE(&buf[12]);
        cal.dig_P5 = S16LE(&buf[14]);
        cal.dig_P6 = S16LE(&buf[16]);
        cal.dig_P7 = S16LE(&buf[18]);
        cal.dig_P8 = S16LE(&buf[20]);
        cal.dig_P9 = S16LE(&buf[22]);
        return cal.dig_T1 != 0 && cal.dig_P1 != 0;
    }

    float compensateTemperature(int32_t adc_T) {
        int32_t var1 = ((((adc_T >> 3) - ((int32_t)cal.dig_T1 << 1))) * ((int32_t)cal.dig_T2)) >> 11;
        int32_t var2 = (((((adc_T >> 4) - ((int32_t)cal.dig_T1)) * ((adc_T >> 4) - ((int32_t)cal.dig_T1))) >> 12) *
                        ((int32_t)cal.dig_T3)) >> 14;
        t_fine = var1 + var2;
        int32_t T = (t_fine * 5 + 128) >> 8;
        return T / 100.0f;
    }

    float compensatePressure(int32_t adc_P) {
        int64_t var1 = ((int64_t)t_fine) - 128000;
        int64_t var2 = var1 * var1 * (int64_t)cal.dig_P6;
        var2 += (var1 * (int64_t)cal.dig_P5) << 17;
        var2 += ((int64_t)cal.dig_P4) << 35;
        var1 = ((var1 * var1 * (int64_t)cal.dig_P3) >> 8) + ((var1 * (int64_t)cal.dig_P2) << 12);
        var1 = (((((int64_t)1) << 47) + var1) * (int64_t)cal.dig_P1) >> 33;
        if (var1 == 0) return 0.0f;
        int64_t p = 1048576 - adc_P;
        p = (((p << 31) - var2) * 3125) / var1;
        var1 = (((int64_t)cal.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
        var2 = (((int64_t)cal.dig_P8) * p) >> 19;
        p = ((p + var1 + var2) >> 8) + (((int64_t)cal.dig_P7) << 4);
        return ((float)p / 256.0f) / 100.0f;
    }
public:
    BMP280_Driver() : Sensor(1000) { data_pack = {}; }
    const char* getName() override { return "BMP280"; }
    uint16_t busAddress() const { return addr; }
    void init() override {
        uint8_t id = 0;
        if (I2cBus::read(0xEC, 0xD0, &id, 1, 10) && id == 0x58) addr = 0xEC;
        else if (I2cBus::read(0xEE, 0xD0, &id, 1, 10) && id == 0x58) addr = 0xEE;
        else { addr = 0; return; }
        uint8_t ctrl = 0x27;
        if (!readCalibration()) { addr = 0; data_pack.is_valid = false; return; }
        if (!I2cBus::write(addr, 0xF4, &ctrl, 1, 10)) { addr = 0; data_pack.is_valid = false; return; }
        data_pack.is_valid = true;
    }
    void update() override {
        if (!addr) { data_pack.is_valid = false; return; }
        if (I2cFault::paused.load(std::memory_order_relaxed)) return;
        if (AuxStop2::restoreFault.load(std::memory_order_relaxed)) {
            data_pack.is_valid = false; return;
        }
        I2cFault::Sample trace(I2cFault::Bmp, addr);
        uint8_t raw[6];
        if (I2cBus::read(addr, 0xF7, raw, 6, 10)) {
            int32_t p = (int32_t)((raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4));
            int32_t t = (int32_t)((raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4));
            data_pack.temperature = compensateTemperature(t);
            data_pack.pressure = compensatePressure(p);
            data_pack.timestamp = HAL_GetTick();
            data_pack.is_valid = true;
        } else {
            data_pack.is_valid = false;
        }
    }
    const void* getData() override { return &data_pack; }
};

#include "global_store.h" 
#include "tim.h" // 提供 htim15

// ==========================================
// 4. 动态无源蜂鸣器执行器 (多频段 PWM 驱动)
// ==========================================
class Buzzer_Driver : public NanoUI::Sensor {
private:
    // Existing user feedback is the higher-priority owner. Game cues reuse this
    // same driver, PWM configuration and 10 ms update; they are just a second
    // owner of the one buzzer, never a separate audio path.
    enum class ToneSource : uint8_t { None = 0, User, Game };

    uint32_t beep_end_time = 0;
    ToneSource source = ToneSource::None;
    uint32_t game_window_start = 0;
    uint8_t game_window_starts = 0;
    struct BuzzerData { bool is_active; } data_pack;

    void silence() {
        __HAL_TIM_SET_COMPARE(&htim15, TIM_CHANNEL_2, 0);
        htim15.Instance->EGR = TIM_EGR_UG; // 立即生效静音
    }

    bool gameRateAllows(uint32_t now) {
        if (static_cast<uint32_t>(now - game_window_start) >= 1000u) {
            game_window_start = now;
            game_window_starts = 0;
        }
        if (game_window_starts >= CrabitTone::kMaxStartsPerSecond) return false;
        ++game_window_starts;
        return true;
    }

    void startTone(uint16_t freq, uint32_t duration_ms, uint32_t now) {
        if (freq < 100) freq = 100;

        uint32_t arr_val = 1000000 / freq;
        __HAL_TIM_SET_AUTORELOAD(&htim15, arr_val);

        // 响度按平方曲线映射，补偿人耳对声压的非线性感知
        // 物理极限：蜂鸣器在 ARR 的 10% 左右就已经接近最大响度
        uint32_t max_effective_duty = arr_val / 10;

        // 音量 0~100 对应占空比按 vol*vol/10000 缩放
        uint32_t active_duty = (max_effective_duty * GlobalStore.sys_volume * GlobalStore.sys_volume) / 10000;

        // 防止低音量时占空比被抹零导致断音
        if (active_duty == 0 && GlobalStore.sys_volume > 0) active_duty = 1;

        __HAL_TIM_SET_COMPARE(&htim15, TIM_CHANNEL_2, active_duty);
        htim15.Instance->EGR = TIM_EGR_UG;

        beep_end_time = now + duration_ms;
    }

public:
    Buzzer_Driver() : Sensor(10) { data_pack = {false}; } 
    
    const char* getName() override { return "BUZZER"; }

    void init() override {
        source = ToneSource::None;
        GlobalStore.req_buzzer_duration = 0;

        // 基础预分频，将 80MHz 降到 1MHz
        __HAL_TIM_SET_PRESCALER(&htim15, 79);
        __HAL_TIM_SET_COMPARE(&htim15, TIM_CHANNEL_2, 0);
        HAL_TIM_PWM_Start(&htim15, TIM_CHANNEL_2);
    }

    void update() override {
        uint32_t now = HAL_GetTick();

        // 1. Existing user feedback request keeps priority and may replace a
        // game tone that is already sounding.
        if (GlobalStore.req_buzzer_duration > 0 && GlobalStore.sys_volume > 0) {
            startTone(GlobalStore.req_buzzer_freq, GlobalStore.req_buzzer_duration, now);
            source = ToneSource::User;
        }
        GlobalStore.req_buzzer_duration = 0; // 吃掉请求

        // 2. Tick-wrap-safe expiry for whichever source currently owns PWM.
        if (source != ToneSource::None &&
            static_cast<int32_t>(now - beep_end_time) >= 0) {
            silence();
            source = ToneSource::None;
        }

        const bool user_active = source == ToneSource::User;
        const bool muted = !CrabitPreferences::enabled(CrabitPreferences::Sound) ||
                           GlobalStore.sys_volume == 0;
        const bool session_active = Crabit::amebaSessionActive() || g_coproc_active;

        // 3. Mute, session loss and user feedback all drop pending game mail.
        // Only a GAME-owned tone may be silenced by these conditions.
        if (!session_active || muted || user_active) {
            CrabitTone::discardPending();
        }
        if ((!session_active || muted) && source == ToneSource::Game) {
            silence();
            source = ToneSource::None;
        }

        // 4. One latest cue per update. PWM is still only touched here.
        if (session_active && !muted && !user_active) {
            const uint32_t word = CrabitTone::take(now);
            if (word == CrabitTone::kStopWord) {
                if (source == ToneSource::Game) {
                    silence();
                    source = ToneSource::None;
                }
            } else if (word != CrabitTone::kEmptyWord && gameRateAllows(now)) {
                const CrabitTone::Tone tone = CrabitTone::decode(word);
                startTone(tone.frequency_hz, tone.duration_ms, now);
                source = ToneSource::Game;
            }
        }

        data_pack.is_active = source != ToneSource::None;
    }

    const void* getData() override { return &data_pack; }
};

#endif
