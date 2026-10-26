#ifndef ARDUBOY_APP_H
#define ARDUBOY_APP_H

#include "ArduboyCore.h"
#include "../AmebaStore.h"

// Frame-driven Arduboy adapter. Games retain their existing setup/loop code,
// but one call advances exactly one frame and always returns to main_loop.
class ArduboyApp : public Application {
protected:
    Arduboy2* ab_ptr = nullptr;
    Canvas* canvas_ptr = nullptr;
    InputDriver* input_ptr = nullptr;
    uint32_t exit_hold_timer = 0;

    virtual void game_setup() = 0;
    virtual void game_loop() = 0;

public:
    AppMode getMode() override { return APP_MODE_FRAME; }

    void onMount(Canvas* canvas, InputDriver* input) override {
        canvas_ptr = canvas;
        input_ptr = input;
        exit_hold_timer = 0;
        if (!ab_ptr) {
            requestExit();
            return;
        }
        ab_ptr->attachCanvas(canvas_ptr);
        game_setup();
    }

    void onUpdate(uint32_t now_ms) override {
        if (!ab_ptr || !canvas_ptr) {
            requestExit();
            return;
        }

        // Consume bridge events without allowing a game frame to monopolize
        // the input driver. The game itself uses the normalized raw mask below.
        if (input_ptr) {
            while (input_ptr->poll().type != EVT_NONE) {
            }
        }

        const uint8_t raw = AmebaStore::raw_btn_mask;
        uint8_t virtual_btns = 0;
        if (raw & (1 << 4)) virtual_btns |= B_BUTTON;
        if (raw & (1 << 5)) virtual_btns |= A_BUTTON;
        if (raw & (1 << 0)) virtual_btns |= UP_BUTTON;
        if (raw & (1 << 1)) virtual_btns |= DOWN_BUTTON;
        if (raw & (1 << 2)) virtual_btns |= LEFT_BUTTON;
        if (raw & (1 << 3)) virtual_btns |= RIGHT_BUTTON;

        const bool is_trying_to_exit = ((raw & (1 << 0)) && (raw & (1 << 1)));
        if (is_trying_to_exit) {
            if (exit_hold_timer == 0) exit_hold_timer = now_ms;
            else if (now_ms - exit_hold_timer > 1000) {
                requestExit();
                return;
            }
        } else {
            exit_hold_timer = 0;
        }

        const float ax = AmebaStore::imu_ax;
        const float ay = AmebaStore::imu_ay;
        const float az = AmebaStore::imu_az;
        if (az > 0.3f) {
            const float deadzone = 0.20f;
            if (ay > deadzone) virtual_btns |= DOWN_BUTTON;
            else if (ay < -deadzone) virtual_btns |= UP_BUTTON;
            if (ax > deadzone) virtual_btns |= RIGHT_BUTTON;
            else if (ax < -deadzone) virtual_btns |= LEFT_BUTTON;
        }

        ab_ptr->updateHwButton(virtual_btns);
        game_loop();

        if (is_trying_to_exit && exit_hold_timer > 0) {
            canvas_ptr->fillRect(34, 28, 60, 8, C_BLACK);
            canvas_ptr->drawRect(34, 28, 60, 8, C_WHITE);
            int bar_w = (int)(((now_ms - exit_hold_timer) * 60U) / 1000U);
            if (bar_w > 60) bar_w = 60;
            canvas_ptr->fillRect(34, 28, bar_w, 8, C_WHITE);
            canvas_ptr->display();
        }
    }

    void onUnmount() override {
        if (canvas_ptr) {
            canvas_ptr->clear();
            canvas_ptr->display();
        }
        canvas_ptr = nullptr;
        input_ptr = nullptr;
    }
};

#endif
