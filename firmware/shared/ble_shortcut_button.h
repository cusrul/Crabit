#ifndef CRABIT_BLE_SHORTCUT_BUTTON_H
#define CRABIT_BLE_SHORTCUT_BUTTON_H

#include <stdint.h>

namespace Crabit {
// Device policy, not a NanoUI event: reserve a single long hold for BLE.
// Short UP is emitted on release so a long hold cannot also navigate the UI.
class BleShortcutButton {
public:
    enum Event { None, Up, ToggleBle };
    void reset(bool pressed) { armed_ = !pressed; tracking_ = fired_ = cancelled_ = false; }
    Event update(bool pressed, bool blocked, uint32_t now) {
        if (!pressed) {
            const bool short_press = tracking_ && !fired_ && !cancelled_ &&
                uint32_t(now - pressed_at_) >= 20;
            armed_ = true;
            tracking_ = fired_ = cancelled_ = false;
            return short_press ? Up : None;
        }
        if (!armed_) return None; // A key held across startup must be released first.
        if (!tracking_) {
            tracking_ = true;
            pressed_at_ = now;
        }
        if (blocked) cancelled_ = true; // Chords never become a single-key shortcut.
        if (!fired_ && !cancelled_ && uint32_t(now - pressed_at_) >= 1000) {
            fired_ = true;
            return ToggleBle;
        }
        return None;
    }
private:
    uint32_t pressed_at_ = 0;
    bool armed_ = false, tracking_ = false, fired_ = false, cancelled_ = false;
};
}
#endif
