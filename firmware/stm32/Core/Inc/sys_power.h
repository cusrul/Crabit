#ifndef SYS_POWER_H
#define SYS_POWER_H

#ifndef CRABIT_DEBUG_STAY_AWAKE
#define CRABIT_DEBUG_STAY_AWAKE 0
#endif
#ifndef CRABIT_POWER_SCREEN_ONLY
#define CRABIT_POWER_SCREEN_ONLY 1
#endif

#include "main.h"
#include <cmath>
#include "sensor_snapshot.h"
#include "usart.h"
#include "i2c.h"
#include "spi.h"
#include "hw_display.h"
#include "hw_sensors.h"
#include "NanoUI/NanoUI.h"
#include "sys_config.h"
#include "event_bus.h" 
#include "badusb_armory.h"
#include "usbd_core.h"   
#include "usb_device.h"  
#include "wake_metrics.h"
#include "mcu_stop2.h"
#include "idle_sleep_policy.h"
extern void STM32_ResetInputAfterWake();

extern SystemConfig sys_cfg;
extern volatile bool g_coproc_active;
extern "C" void STM32_RestartUartRx(void);
extern volatile uint32_t g_qmi_int1_cnt; 
extern volatile uint32_t g_qmi_tap_cnt;
extern volatile uint32_t g_qmi_tap_tick;

extern "C" {
    void OLED_Init(void); 
}
extern void OLED_SetSleep(bool sleep);
extern "C" void SystemClock_Config(void);
extern "C" void MX_USART1_UART_Init(void);
extern "C" void MX_I2C1_Init(void);
extern "C" void MX_SPI3_Init(void);
extern "C" void MX_USB_DEVICE_Init(void); 
extern USBD_HandleTypeDef hUsbDeviceFS;

// 外设引用
extern PAT9125_Driver pat_drv;
extern QMI8658_Driver imu_drv;   
extern BMP280_Driver bmp_drv;
extern WindowManager wm;
extern uint8_t rx_pingpong[2][512]; 
extern volatile uint8_t rx_write_idx;
extern volatile uint32_t g_pat_int_cnt;

enum SysState { STATE_ACTIVE, STATE_SCREEN_OFF, STATE_SLEEP_PREP, STATE_DEEP_SLEEP, STATE_POWER_FAULT };

class SysPower {
private:
    static SysState current_state;
    static uint32_t last_activity_tick;
    static uint32_t last_qmi_ints;
    static uint32_t last_pat_ints;
    static bool was_in_zone;
    static bool sensor_wake_locked;
    static bool last_pa6_pressed;
    static uint32_t last_qmi_taps;
    static uint32_t screen_off_tick;
    static uint32_t zone_since;
    static uint32_t last_imu_tick;

    static void applyDisplayConfig() {
        uint8_t contrast = (uint8_t)(sys_cfg.brightness * 255.0f);
        OLED_WR_Byte(0x81, 0); 
        OLED_WR_Byte(contrast, 0); 
        OLED_WR_Byte(sys_cfg.invert_disp ? 0xA7 : 0xA6, 0);
    }

    static void armPa6OnlyStop2Wake() {
        GPIO_InitTypeDef GPIO_InitStruct = {0};

        // Sensor EXTI lines can be left pending and wake STOP2 immediately.
        HAL_NVIC_DisableIRQ(EXTI0_IRQn);
        HAL_NVIC_DisableIRQ(EXTI1_IRQn);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_1);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_8);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_14);
        HAL_NVIC_ClearPendingIRQ(EXTI0_IRQn);
        HAL_NVIC_ClearPendingIRQ(EXTI1_IRQn);

        // Deep sleep wake is PA6 only: button pulls the internal pull-up to GND.
        GPIO_InitStruct.Pin = GPIO_PIN_6;
        GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_6);
        HAL_NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
        HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    }

    static void restoreRunWakeInputs() {
        GPIO_InitTypeDef GPIO_InitStruct = {0};

        HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_6);
        HAL_NVIC_ClearPendingIRQ(EXTI9_5_IRQn);

        GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_1);
        HAL_NVIC_ClearPendingIRQ(EXTI0_IRQn);
        HAL_NVIC_ClearPendingIRQ(EXTI1_IRQn);
        HAL_NVIC_EnableIRQ(EXTI0_IRQn);
        HAL_NVIC_EnableIRQ(EXTI1_IRQn);
    }

    static void prepareDeepSleep() {
        current_state = STATE_SLEEP_PREP;
        // Retain the App/Widget stack and focus; STOP2 preserves SRAM.
    }

    static void restoreFromScreenOff() {
        // This path never lowered DC1 or cut the display rail. Do not reset
        // OLED or rewrite PMIC on an ordinary screen-only wake.
        OLED_SetSleep(false);
        applyDisplayConfig();
        current_state = STATE_ACTIVE;
        feed();
        STM32_ResetInputAfterWake();
        if (WindowManager::instance) WindowManager::instance->markDirty();
    }

    static void enterDeepSleep() {
        sensor_wake_locked = true;
        current_state = STATE_DEEP_SLEEP;
        const McuStop2::Result result = McuStop2::runWatch();
        WakeMetrics::stop_reason.store(uint32_t(result.reason), std::memory_order_relaxed);
        WakeMetrics::stop_seconds.store(result.seconds, std::memory_order_relaxed);
        WakeMetrics::stop_hclk.store(result.hclk, std::memory_order_relaxed);
        if (result.waited) WakeMetrics::wakes.fetch_add(1, std::memory_order_relaxed);
        if (!result.waited && result.reason == McuStop2::Reason::MotionPending) {
            // Keep the screen deadline expired; the existing retry limiter applies.
            current_state = STATE_SCREEN_OFF;
            screen_off_tick = HAL_GetTick();
            was_in_zone = false;
            last_qmi_taps = g_qmi_tap_cnt;
            return;
        }
        // Sensor registers were restored before tasks resumed; rails and OLED
        // were retained. SENS failures inhibit further sensor use until reboot.
        OLED_SetSleep(false);
        applyDisplayConfig();

        current_state = STATE_ACTIVE;
        feed();
        STM32_ResetInputAfterWake();
        last_qmi_taps = g_qmi_tap_cnt;
        if (WindowManager::instance) WindowManager::instance->markDirty();
    }

public:
    static void init() {
        current_state = STATE_ACTIVE;
        last_activity_tick = HAL_GetTick();
        last_qmi_ints = g_qmi_int1_cnt;
        last_pat_ints = g_pat_int_cnt;
        was_in_zone = false;
        sensor_wake_locked = false;
        last_pa6_pressed = false;
        last_qmi_taps = g_qmi_tap_cnt;
        screen_off_tick = zone_since = last_imu_tick = 0;
    }

    static void feed() {
        last_activity_tick = HAL_GetTick();
    }

    static bool isActive() {
        return current_state == STATE_ACTIVE;
    }

    static bool canPollGesture() {
        return current_state == STATE_SCREEN_OFF &&
            (sys_cfg.enable_lift_wake || sys_cfg.enable_tap_wake);
    }

    static void tick() {
        // Diagnostic builds boot ACTIVE and remain awake without changing the
        // saved sleep settings or invoking any power/display recovery sequence.
        if (CRABIT_DEBUG_STAY_AWAKE) { feed(); return; }
        uint32_t now = HAL_GetTick();
        uint32_t idle_time = now - last_activity_tick;

        if (g_coproc_active || sys_cfg.screen_off_ms == 0) { 
            idle_time = 0; 
            last_activity_tick = now; 
        }

        // --- 1. 唤醒条件检测 ---
        bool pa6_pressed = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET;
        const bool pa6_press_edge = pa6_pressed && !last_pa6_pressed;
        last_pa6_pressed = pa6_pressed;
        if (current_state == STATE_POWER_FAULT) {
            // A failed write/readback must not create a rapid automatic retry
            // loop. Release then press PA6 to request one further check.
            if (pa6_press_edge) restoreFromScreenOff();
            return;
        }
        bool button_pressed = pa6_pressed;
        if (current_state == STATE_ACTIVE) {
            button_pressed = (
                pa6_pressed ||
                HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET ||
                HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_SET
            );
        }

        bool current_in_zone = false;
        StmSensorSnapshot sensor_snapshot{};
        const bool fresh_imu = STM32_ReadSensorSnapshot(&sensor_snapshot) &&
            sensor_snapshot.imu.is_valid && uint32_t(now - sensor_snapshot.imu.timestamp) <= 200 &&
            std::isfinite(sensor_snapshot.imu.ax) && std::isfinite(sensor_snapshot.imu.ay) &&
            std::isfinite(sensor_snapshot.imu.az);
        if (fresh_imu) {
            const auto* data = &sensor_snapshot.imu;
            float ay_abs = fabs(data->ay);
            if (fabs(data->ax) < sys_cfg.wake_x_max && ay_abs > sys_cfg.wake_y_min && 
                ay_abs < sys_cfg.wake_y_max && data->az > sys_cfg.wake_z_min) {
                current_in_zone = true; 
            }
        }

        bool valid_tap = false, valid_wrist = false;
        const uint32_t taps = g_qmi_tap_cnt;
        const uint32_t tap_tick = g_qmi_tap_tick;
        const bool new_tap = taps != last_qmi_taps;
        last_qmi_taps = taps; // Consume disabled, stale and pre-guard events too.
        if (current_state == STATE_ACTIVE || current_state == STATE_SCREEN_OFF) {
            if (sys_cfg.enable_tap_wake && new_tap && uint32_t(now - tap_tick) <= 200) {
                valid_tap = current_state == STATE_ACTIVE ||
                    (current_state == STATE_SCREEN_OFF &&
                     uint32_t(tap_tick - screen_off_tick) >= 500 &&
                     uint32_t(tap_tick - screen_off_tick) <= uint32_t(now - screen_off_tick));
            }
            if (current_state == STATE_SCREEN_OFF && sys_cfg.enable_lift_wake && fresh_imu) {
                if (!current_in_zone) {
                    // Arm only after a real, fresh departure from the viewing pose.
                    sensor_wake_locked = false;
                    was_in_zone = false;
                } else if (!sensor_wake_locked) {
                    const uint32_t sample_tick = sensor_snapshot.imu.timestamp;
                    if (!was_in_zone || uint32_t(sample_tick - last_imu_tick) > 100)
                        zone_since = sample_tick;
                    was_in_zone = true;
                    valid_wrist = uint32_t(now - screen_off_tick) >= 500 &&
                        uint32_t(sample_tick - zone_since) >= 150;
                }
            } else {
                was_in_zone = false;
            }
        }
        if (fresh_imu) last_imu_tick = sensor_snapshot.imu.timestamp;
        last_qmi_ints = g_qmi_int1_cnt;
        last_pat_ints = g_pat_int_cnt;

        if (current_state == STATE_SCREEN_OFF && (valid_tap || valid_wrist))
            USB_LOG("[PWR] wake request=%s\r\n", valid_tap ? "QMI_TAP" : "QMI_LIFT");
        if (button_pressed || valid_tap || valid_wrist) { feed(); idle_time = 0; }

        // --- 2. 状态机流转 ---
        if (sys_cfg.screen_off_ms == 0 || idle_time < sys_cfg.screen_off_ms) {
            if (current_state != STATE_ACTIVE) { 
                restoreFromScreenOff();
            }
        } 
        else {
            if (current_state == STATE_ACTIVE) { 
                OLED_SetSleep(true); 
                current_state = STATE_SCREEN_OFF; sensor_wake_locked = true; 
                screen_off_tick = now;
                was_in_zone = false;
                last_qmi_taps = g_qmi_tap_cnt;
            }

            // Go directly to STOP2 at the screen deadline when safe. Nonzero
            // legacy deep_sleep_ms enables it; no second waiting period.
            if (IdleSleep::due(CRABIT_POWER_SCREEN_ONLY, sys_cfg.screen_off_ms,
                               sys_cfg.deep_sleep_ms, idle_time) &&
                current_state != STATE_DEEP_SLEEP) {
                if (BadUsbArmory::hasPendingTasks()) return;
                static uint32_t last_stop_check = 0;
                if (uint32_t(now - last_stop_check) < 1000u) return;
                last_stop_check = now;
                McuStop2::WatchPort port;
                const McuStop2::Reason blocked = port.blocked();
                if (blocked != McuStop2::Reason::None) {
                    WakeMetrics::stop_reason.store(uint32_t(blocked), std::memory_order_relaxed);
                    return;
                }
                prepareDeepSleep();
                enterDeepSleep();
            }
        }
    }
};

inline SysState SysPower::current_state = STATE_ACTIVE;
inline uint32_t SysPower::last_activity_tick = 0;
inline uint32_t SysPower::last_qmi_ints = 0;
inline uint32_t SysPower::last_pat_ints = 0;
inline bool SysPower::was_in_zone = false;
inline bool SysPower::sensor_wake_locked = false;
inline bool SysPower::last_pa6_pressed = false;
inline uint32_t SysPower::last_qmi_taps = 0;
inline uint32_t SysPower::screen_off_tick = 0;
inline uint32_t SysPower::zone_since = 0;
inline uint32_t SysPower::last_imu_tick = 0;

#endif
