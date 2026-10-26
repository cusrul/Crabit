#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "NanoUI/NanoUI.h"
#include "components/action_button.h"
#include "components/settings_option_picker.h"
#include "components/ble_controls.h"
#include "sys_config.h"
#include "global_store.h"
#include <cmath>
#include <cstdio>

extern "C" void OLED_WR_Byte(uint8_t dat, uint8_t cmd);
#if CRABIT_UI_ALLOC_DIAG
extern "C" void STM32_UiPoolCheckpoint(unsigned stage);
#endif

// Display/power drafts use APPLY. Bluetooth is an immediate runtime action;
// never persist it through the legacy enable_ble boot configuration.
class SettingsApp : public Application {
    // Keep the existing picker choices; preserve custom/remote timeout values.
    static constexpr uint32_t off_times[] = {3000, 5000, 10000, 15000, 30000};
    static constexpr int off_custom = sizeof(off_times) / sizeof(off_times[0]);

    float brightness = 0.5f;
    bool inverted = false;
    bool deep_sleep_enabled = true;
    bool pat_input = true, qmi_input = true, lift_wake = true, tap_wake = true;
    bool key_sound = true, key_led = true, save_failed = false;
    float volume = 100, led_level = 8;
    int off_index = 0, face_index = 0;
    uint32_t original_off = 0, original_sleep = 0;
    bool display_previewed = false;
    NanoUI::NanoUIText title = "设置", brightness_text;
    NanoUI::NanoUIText ble_status_text = "OFF", ble_action_text = "ON";
    NanoUI::StaticVector<const char*, 8> off_labels;
    NanoUI::StaticVector<const char*, 8> face_labels = {"反白底栏", "点阵", "极简", "左右分区",
                                                        "双行时刻"};

    static float validBrightness(float value) {
        return std::isfinite(value) && value >= 0.0f && value <= 1.0f ? value : 0.5f;
    }

    static int timeIndex(uint32_t value, const uint32_t* choices, int count) {
        for (int i = 0; i < count; ++i)
            if (value == choices[i])
                return i;
        return count;
    }

    uint32_t screenOff() const {
        return off_index == off_custom ? original_off : off_times[off_index];
    }

    uint32_t sleepDelay() const {
        // Keep nonzero legacy/custom values intact on Apply; only enable/disable
        // is exposed now, with screenOff() providing the single timeout.
        return deep_sleep_enabled ? (original_sleep ? original_sleep : 10000u) : 0u;
    }

    bool changed() const {
        const auto& p = CrabitPreferences::current;
        return save_failed || pat_input != CrabitPreferences::enabled(CrabitPreferences::Pat) ||
               qmi_input != CrabitPreferences::enabled(CrabitPreferences::Qmi) ||
               key_sound != CrabitPreferences::enabled(CrabitPreferences::Sound) ||
               key_led != CrabitPreferences::enabled(CrabitPreferences::Led) ||
               uint8_t(volume) != p.volume || uint8_t(led_level) != p.led_level ||
               lift_wake != sys_cfg.enable_lift_wake || tap_wake != sys_cfg.enable_tap_wake ||
               !std::isfinite(sys_cfg.brightness) || brightness != sys_cfg.brightness ||
               inverted != sys_cfg.invert_disp || screenOff() != sys_cfg.screen_off_ms ||
               sleepDelay() != sys_cfg.deep_sleep_ms || face_index != sys_cfg.watch_face;
    }

    void updateLabels() {
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%d%%",
                      static_cast<int>(brightness * 100.0f + 0.5f));
        brightness_text = percent;
        title = changed() ? "设置 *" : "设置";
    }

    // Retain the existing OLED preview commands; no power-domain changes.
    static void previewBrightness(float value) {
        OLED_WR_Byte(0x81, 0);
        OLED_WR_Byte(static_cast<uint8_t>(value * 255.0f), 0);
        OLED_WR_Byte(0xD9, 0);
        OLED_WR_Byte(value < 0.15f ? 0x11 : 0x22, 0);
        OLED_WR_Byte(0xDB, 0);
        OLED_WR_Byte(value < 0.15f ? 0x00 : 0x20, 0);
    }

    void onBrightness(float value) {
        brightness = validBrightness(value);
        previewBrightness(brightness);
        display_previewed = true;
        updateLabels();
    }

    void onInvert(bool value) {
        inverted = value;
        OLED_WR_Byte(inverted ? 0xA7 : 0xA6, 0);
        display_previewed = true;
        updateLabels();
    }

    void apply() {
        if (!changed()) {
            Popup::showToast("没有修改");
            return;
        }
        // Commit only this page's fields, never a stale whole-config snapshot.
        sys_cfg.brightness = brightness;
        sys_cfg.invert_disp = inverted;
        sys_cfg.screen_off_ms = screenOff();
        sys_cfg.deep_sleep_ms = sleepDelay();
        sys_cfg.watch_face = static_cast<uint8_t>(face_index);
        sys_cfg.enable_lift_wake = lift_wake;
        sys_cfg.enable_tap_wake = tap_wake;
        auto& p = CrabitPreferences::current;
        p.flags =
            (pat_input ? CrabitPreferences::Pat : 0) | (qmi_input ? CrabitPreferences::Qmi : 0) |
            (key_sound ? CrabitPreferences::Sound : 0) | (key_led ? CrabitPreferences::Led : 0);
        p.volume = uint8_t(volume);
        p.led_level = uint8_t(led_level);
        GlobalStore.sys_volume = p.volume;
        const auto result = ConfigManager::save();  // Existing storage driver/address, unchanged.
        save_failed = result != ConfigManager::SaveResult::Saved;
        updateLabels();
        Popup::showToast(result == ConfigManager::SaveResult::Saved ? "已应用" : "保存失败");
    }

    static Widget* settingRow(const char* label, Widget* control) {
        // Row centers its children vertically; share the row's height and side padding.
        return new Padding(
            new ConstrainedBox(new Row({new Text(label, 8), new Spacer(), control}),
                               BoxConstraints{0, 32767, 18, 18}),
            Insets{4, 0, 4, 0});
    }

   public:
    const char* getAppName() override { return "SETTINGS"; }

    void onTick() override {
        const char* status = Crabit::CompanionBle::status();
        const char* action = Crabit::CompanionBle::active() ? "OFF" : "ON";
        if (strcmp(ble_status_text.c_str(), status) || strcmp(ble_action_text.c_str(), action)) {
            ble_status_text = status;
            ble_action_text = action;
            if (WindowManager::instance)
                WindowManager::instance->markDirty();
        }
    }

    void onCreate() override {
#if CRABIT_UI_ALLOC_DIAG
        STM32_UiPoolCheckpoint(1);
#endif
        brightness = validBrightness(sys_cfg.brightness);
        inverted = sys_cfg.invert_disp;
        original_off = sys_cfg.screen_off_ms;
        original_sleep = sys_cfg.deep_sleep_ms;
        off_index = timeIndex(original_off, off_times, off_custom);
        deep_sleep_enabled = original_sleep != 0;
        face_index = Crabit::validWatchFace(sys_cfg.watch_face);
        pat_input = CrabitPreferences::enabled(CrabitPreferences::Pat);
        qmi_input = CrabitPreferences::enabled(CrabitPreferences::Qmi);
        key_sound = CrabitPreferences::enabled(CrabitPreferences::Sound);
        key_led = CrabitPreferences::enabled(CrabitPreferences::Led);
        volume = CrabitPreferences::current.volume;
        led_level = CrabitPreferences::current.led_level;
        lift_wake = sys_cfg.enable_lift_wake;
        tap_wake = sys_cfg.enable_tap_wake;
        save_failed = false;
        off_labels = {"3s", "5s", "10s", "15s", "30s"};
        // Do not silently turn a value written elsewhere into NEVER on Apply.
        if (off_index == off_custom)
            off_labels.push_back("自定义");
        display_previewed = false;
        updateLabels();
        onTick();
    }

    void onDestroy() override {
        // Back (including while editing) discards the draft and exits. Restore
        // the current committed display settings, also after an earlier Apply.
        if (display_previewed) {
            previewBrightness(validBrightness(sys_cfg.brightness));
            OLED_WR_Byte(sys_cfg.invert_disp ? 0xA7 : 0xA6, 0);
        }
    }

    Widget* buildUI() override {
#if CRABIT_UI_ALLOC_DIAG
        STM32_UiPoolCheckpoint(2);
#endif
        auto changedValue = [this]() { updateLabels(); };
        return new Container(
            new Column(
                {new Container(new SlidingText(&title, 8), 128, 12),
                 new Expanded(new ScrollColumn(
                     {settingRow(
                          "亮度",
                          new Row({new Slider(brightness, 1.0f, 28,
                                              [this](float value) { onBrightness(value); }),
                                   new SizedBox(2, 0),
                                   new Container(new SlidingText(&brightness_text, 8), 24, 8)})),
                      settingRow("反色",
                                 new Switch(inverted, [this](bool value) { onInvert(value); })),
                      settingRow("息屏",
                                 new SettingsOptionPicker(&off_index, off_labels, changedValue)),
                      settingRow("深睡",
                                 new Switch(deep_sleep_enabled, [this](bool) { updateLabels(); })),
                      settingRow("触摸操作",
                                 new Switch(pat_input, [this](bool) { updateLabels(); })),
                      settingRow("倾斜操作",
                                 new Switch(qmi_input, [this](bool) { updateLabels(); })),
                      settingRow("抬腕唤醒",
                                 new Switch(lift_wake, [this](bool) { updateLabels(); })),
                      settingRow("敲击唤醒",
                                 new Switch(tap_wake, [this](bool) { updateLabels(); })),
                      settingRow("按键声音",
                                 new Switch(key_sound, [this](bool) { updateLabels(); })),
                      settingRow("音量",
                                 new Slider(volume, 100.0f, 48, [this](float) { updateLabels(); })),
                      settingRow("按键灯光", new Switch(key_led, [this](bool) { updateLabels(); })),
                      settingRow("灯光亮度", new Slider(led_level, 100.0f, 48,
                                                        [this](float) { updateLabels(); })),
                      settingRow(
                          "蓝牙",
                          new Button(new Container(new SlidingText(&ble_action_text, 8), 36, 14),
                                     []() { Crabit::toggleBleFromUi(); })),
                      new Container(new SlidingText(&ble_status_text, 8), 128, 10),
                      settingRow("表盘",
                                 new SettingsOptionPicker(&face_index, face_labels, changedValue)),
                      new SizedBox(0, 4),
                      new Row({new Spacer(), new ActionButton("应用", [this]() { apply(); }),
                               new SizedBox(8, 0),
                               new ActionButton("返回", []() { AppManager::exitCurrentApp(); }),
                               new Spacer()}),
                      new SizedBox(0, 4)}))}),
            128, 64);
    }
};

#endif
