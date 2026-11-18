#include "settings_service.h"

#include <string.h>

#include "ameba_session.h"
#include "comms_link.h"
#include "display.h"
#include "global_store.h"
#include "product_preferences.h"
#include "settings_contract.h"
#include "sys_config.h"

extern volatile bool g_coproc_active;

    static bool handlePreferences(const CrabitWire::Message& message) {
        using namespace CrabitPreferences;
        const bool read = strcmp(message.topic, READ_TOPIC) == 0;
        const bool set = strcmp(message.topic, SET_TOPIC) == 0;
        if (!read && !set) return false;
        const char* result = "invalid";
        const char* error = "payload";
        if (read && message.value_len == 4 && memcmp(message.value, "read", 4) == 0) {
            result = "snapshot"; error = "none";
        } else if (set && Crabit::amebaTransitionActive()) {
            result = "busy"; error = "owner";
        } else if (set) {
            Patch patch;
            if (parse(message.value, message.value_len, patch)) {
                applyRecord(patch);
                if (patch.fields & (1u << LiftKey)) sys_cfg.enable_lift_wake = patch.values[LiftKey];
                if (patch.fields & (1u << TapKey)) sys_cfg.enable_tap_wake = patch.values[TapKey];
                if (patch.fields & (1u << FaceKey)) sys_cfg.watch_face = patch.values[FaceKey];
                GlobalStore.sys_volume = current.volume;
                const auto saved = ConfigManager::save();
                result = saved == ConfigManager::SaveResult::Saved ? "saved" : "save_failed";
                error = ConfigManager::saveError(saved);
                if (WindowManager::instance) WindowManager::instance->markDirty();
            }
        }
        char reply[CrabitWire::MAX_VALUE + 1];
        if (response(reply, sizeof(reply), result, error, sys_cfg.enable_lift_wake,
                     sys_cfg.enable_tap_wake, sys_cfg.watch_face, false))
            CrabitLink::send(message.topic, reply, message.request_id);
        return true;
    }

    static bool handleSettings(const CrabitWire::Message& message) {
        const bool read = strcmp(message.topic, CrabitSettings::READ_TOPIC) == 0;
        const bool set = strcmp(message.topic, CrabitSettings::SET_TOPIC) == 0;
        if (!read && !set) return false;
        const char* result = "invalid";
        const char* error = "payload";
        bool display_deferred = false;
        if (read && message.value_len == 4 && memcmp(message.value, "read", 4) == 0) {
            result = "snapshot"; error = "none";
        } else if (set && Crabit::amebaTransitionActive()) {
            result = "busy"; error = "owner";
        } else if (set) {
            CrabitSettings::Patch patch;
            if (CrabitSettings::parse(message.value, message.value_len, patch)) {
                display_deferred = (patch.fields & (CrabitSettings::Brightness | CrabitSettings::Invert)) &&
                    (Crabit::amebaSessionActive() || g_coproc_active);
                const auto saved = ConfigManager::apply(patch);
                result = saved == ConfigManager::SaveResult::Saved ? "saved" : "save_failed";
                error = ConfigManager::saveError(saved);
                // Existing SettingsApp contrast/precharge/VCOM/invert sequence,
                // using CURRENT RAM config, only while STM32 owns OLED. Do not
                // wake the panel, switch buses, or touch BW16's handed-over OLED.
                if (!g_coproc_active && !Crabit::amebaSessionActive() &&
                    (patch.fields & (CrabitSettings::Brightness | CrabitSettings::Invert))) {
                    Display::setBrightness(sys_cfg.brightness);
                    Display::setInvert(sys_cfg.invert_disp);
                }
                if (WindowManager::instance) WindowManager::instance->markDirty();
            }
        }
        const char* display = "unchanged";
        if (display_deferred) display = "deferred";
        char value[CrabitWire::MAX_VALUE + 1];
        if (ConfigManager::settingsResponse(value, sizeof(value), result, error, display)) {
            CrabitLink::send(message.topic, value, message.request_id);
        }
        return true;
    }

bool SettingsService::handle(const CrabitWire::Message& message) {
    if (handlePreferences(message)) return true;
    return handleSettings(message);
}
