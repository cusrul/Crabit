
#ifndef HW_INPUT_H
#define HW_INPUT_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/sensor_manager.h"
#include "NanoUI/core/sensor_types.h"
#include "main.h" 
#include <stdlib.h>
#include <math.h>
#include "sys_power.h" 
#include "global_store.h" 
#include "sensor_snapshot.h"
#include "../../../shared/ble_shortcut_button.h"
#include "app_real/components/ble_controls.h"
#include "input_feedback.h"
#include "../../../shared/product_preferences.h"
#if CRABIT_QMI_RUN_PROBE
#include "qmi_run_probe.h"
#endif

class STM32Input : public InputDriver {
private:
    // --- PAT State (Optical Touch) ---
    int accum_x = 0, accum_y = 0;
    const int SWIPE_THRESH = 100; 
    bool is_touching = false, is_swiping = false;
    uint32_t touch_start_time = 0, last_swipe_time = 0; 
    uint8_t pat_tap_count = 0;
    uint32_t pat_tap_timer = 0;

    // --- Physical Button State (Schematic Alignment) ---
    // SW3 (PA7) - Left Top: UP
    // SW2 (PA6) - Left Bottom: DOWN
    // SW4 (PH3) - Right Top: OK (Short) / BACK (Long)
    uint8_t last_sw_dn, last_sw_ok;
    Crabit::BleShortcutButton up_button;
    uint32_t last_trigger_time = 0; 
    uint32_t ok_press_start_time = 0;
    bool is_ok_pressed = false;
    bool back_was_triggered = false;

    // --- QMI8658 State (IMU) ---
    bool is_tilted = false;
    int tilt_direction = 0;
    uint32_t tilt_recovery_time = 0; 
    IdleSleep::WakeInputGuard wake_input_guard;

    UIEvent dispatch(UIEvent e, bool physical = false) {
        if (e.type != EVT_NONE) {
            SysPower::feed(); 
            if (physical) InputFeedback::press();
            if (GlobalStore.sys_volume > 0 && CrabitPreferences::enabled(CrabitPreferences::Sound)) {
                switch (e.type) {
                    case EVT_UP: case EVT_DOWN: case EVT_LEFT: case EVT_RIGHT:
                        SysAudio::play(2500, 10); break;
                    case EVT_OK:
                        SysAudio::play(3500, 20); break;
                    case EVT_BACK:
                        SysAudio::play(1500, 30); break;
                    default:
                        SysAudio::play(2000, 15); break;
                }
            }
        }
        return e;
    }

public:
    void resetAfterWake() {
        init();
        accum_x = accum_y = 0;
        is_touching = is_swiping = false;
        pat_tap_count = 0;
        is_ok_pressed = back_was_triggered = false;
        is_tilted = false;
        tilt_direction = 0;
        last_trigger_time = tilt_recovery_time = HAL_GetTick();
        wake_input_guard.arm(last_trigger_time);
    }
    void init() override { 
        up_button.reset(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET); // SW3
        last_sw_dn = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6); // SW2
        last_sw_ok = HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3); // SW4
    }
    
    UIEvent poll() override {
        uint32_t now = HAL_GetTick();
        InputFeedback::tick();
#if CRABIT_QMI_RUN_PROBE
        // Moving the watch during this timed probe must not navigate or start BLE.
        if (QmiRunProbe::active.load(std::memory_order_relaxed)) {
            init();
            return {EVT_NONE, 0};
        }
#endif
        const bool wake_key_held = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET ||
            HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET ||
            HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) != GPIO_PIN_RESET;
        if (wake_input_guard.consume(now, wake_key_held)) {
            init();
            return {EVT_NONE, 0};
        }

        // Service physical UP before sensors can return early. Keep shortcut
        // ownership in the UI task, never a GPIO interrupt or sensor worker.
        const bool up_pressed = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET;
        const bool other_pressed = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6) == GPIO_PIN_RESET ||
            HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) != GPIO_PIN_RESET;
        const auto up_event = up_button.update(up_pressed, other_pressed, now);
        if (up_pressed) SysPower::feed();
        if (up_event == Crabit::BleShortcutButton::ToggleBle) {
            last_trigger_time = now;
            dispatch({EVT_OK, 0}, true);
            Crabit::toggleBleFromUi();
            return {EVT_NONE, 0};
        }
        if (up_event == Crabit::BleShortcutButton::Up && now - last_trigger_time > 150) {
            last_trigger_time = now;
            return dispatch({EVT_UP, 0}, true);
        }

        // --- 1. IMU Logic ---
        StmSensorSnapshot sensor_snapshot{};
        if (STM32_ReadSensorSnapshot(&sensor_snapshot) && sensor_snapshot.imu.is_valid &&
            CrabitPreferences::enabled(CrabitPreferences::Qmi)) {
            const auto* idata = &sensor_snapshot.imu;
            float ax = idata->ax; float ay = idata->ay; float az = idata->az;
            auto current_tilt = EVT_NONE;
            if (az > 0.4f && (now - tilt_recovery_time > 300)) {
                if (fabsf(ay) > fabsf(ax)) {
                    if (ay > 0.35f) current_tilt = EVT_DOWN;       
                    else if (ay < -0.35f) current_tilt = EVT_UP; 
                } else {
                    if (ax > 0.35f) current_tilt = EVT_RIGHT;       
                    else if (ax < -0.35f) current_tilt = EVT_LEFT; 
                }
            }
            if (current_tilt != EVT_NONE) {
                if (!is_tilted || tilt_direction != current_tilt) {
                    is_tilted = true; tilt_direction = current_tilt;
                    return dispatch({current_tilt, 0}); 
                }
            } else {
                if (is_tilted) tilt_recovery_time = now;
                is_tilted = false; 
            }
        } else { is_tilted = false; tilt_direction = 0; }

        // --- 2. PAT9125 Logic ---
        if (sensor_snapshot.optical.is_valid && CrabitPreferences::enabled(CrabitPreferences::Pat)) {
            const auto* pdata = &sensor_snapshot.optical;
            bool current_touch = !pdata->lifted; 
            if (current_touch && !is_touching) {
                touch_start_time = now; accum_x = 0; accum_y = 0; is_swiping = false; 
                if (now - pat_tap_timer > 300) pat_tap_count = 0;
            } else if (current_touch && is_touching) {
                accum_x += pdata->dx; accum_y += pdata->dy;
                if (abs(accum_x) > SWIPE_THRESH || abs(accum_y) > SWIPE_THRESH) {
                    is_swiping = true; pat_tap_count = 0; 
                    if (now - last_swipe_time > 60) {
                        last_swipe_time = now; 
                        int res_x = accum_x, res_y = accum_y; accum_x = 0; accum_y = 0;
                        if (abs(res_x) > abs(res_y)) return dispatch({res_x > 0 ? EVT_RIGHT : EVT_LEFT, 0}); 
                        else                         return dispatch({res_y > 0 ? EVT_DOWN : EVT_UP, 0});    
                    }
                }
            } else if (!current_touch && is_touching) {
                uint32_t duration = now - touch_start_time;
                if (!is_swiping && duration > 20 && duration < 400) {
                    pat_tap_count++; pat_tap_timer = now; 
                }
            }
            is_touching = current_touch;
            UIEvent pt_evt = {EVT_NONE, 0};
            if (pat_tap_count > 0 && !is_touching) {
                if (pat_tap_count >= 2) { pat_tap_count = 0; pt_evt.type = EVT_BACK; }
                else if (pat_tap_count == 1 && (now - pat_tap_timer > 220)) { pat_tap_count = 0; pt_evt.type = EVT_OK; }
            }
            if (pt_evt.type != EVT_NONE) return dispatch(pt_evt); 
        } else {
            is_touching = false; is_swiping = false;
            accum_x = accum_y = 0; pat_tap_count = 0;
        }

        // --- 3. Physical Buttons (Schematic: Left UP/DOWN, Right OK/BACK) ---
        UIEvent btn_evt = {EVT_NONE, 0};
        uint8_t sw_dn = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6); // SW2: Left Bottom
        uint8_t sw_ok = HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3); // SW4: Right Top

        // Handle SW4 Multiplexing: Short -> OK, Long -> BACK
        // Detect Press (0 -> 1)
        if (last_sw_ok == 0 && sw_ok == 1) {
            ok_press_start_time = now;
            is_ok_pressed = true;
            back_was_triggered = false;
        }
        
        // Detect Long Press (While holding)
        if (is_ok_pressed && sw_ok == 1 && !back_was_triggered) {
            if (now - ok_press_start_time > 550) { // Long press threshold: 550ms
                btn_evt.type = EVT_BACK;
                back_was_triggered = true;
                last_trigger_time = now;
            }
        }

        // Detect Release (1 -> 0)
        if (last_sw_ok == 1 && sw_ok == 0) {
            if (is_ok_pressed && !back_was_triggered) {
                uint32_t duration = now - ok_press_start_time;
                if (duration > 20 && duration < 550) { // Valid short press
                    btn_evt.type = EVT_OK;
                    last_trigger_time = now;
                }
            }
            is_ok_pressed = false;
        }

        // Handle UP/DOWN (Falling Edge: 1 -> 0)
        if (btn_evt.type == EVT_NONE && (now - last_trigger_time > 150)) {
            if (last_sw_dn == 1 && sw_dn == 0) { btn_evt.type = EVT_DOWN; last_trigger_time = now; }
        }
        
        last_sw_dn = sw_dn; last_sw_ok = sw_ok;

        return dispatch(btn_evt, true);
    }
};
#endif
