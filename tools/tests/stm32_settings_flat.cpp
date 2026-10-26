// Runtime fixture for test_stm32_settings_flat.py. This mounts the product
// SettingsApp through AppManager and drives the shared WindowManager.
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <limits>

#include "NanoUI.h"
#include "app_settings.h"
#include "sys_config.h"
#include "settings_flat_fakes/fake_hal.h"

SystemConfig sys_cfg;
GlobalDataStore GlobalStore;
namespace Crabit { namespace CompanionBle {
bool fake_active=false, fake_transitioning=false;
unsigned toggle_calls=0;
bool active() { return fake_active; }
bool transitioning() { return fake_transitioning; }
const char* status() { return fake_active ? "ADVERTISING" : "OFF"; }
bool toggle() { ++toggle_calls; fake_active=!fake_active; return true; }
} }
unsigned settings_flat_flash_unlock_calls = 0;
unsigned settings_flat_flash_erase_calls = 0;
unsigned settings_flat_flash_program_calls = 0;
unsigned settings_flat_program_fail_call = 0;
unsigned settings_flat_flash_lock_calls = 0;
uint8_t settings_flat_flash_image[56]{};
static_assert(sizeof(SystemConfig) == 48, "the legacy configuration image remains 48 bytes");
static_assert(sizeof(settings_flat_flash_image) == 48 + sizeof(CrabitPreferences::Record),
              "the optional preference trailer follows the unchanged legacy image");
int settings_flat_unlock_status = HAL_OK;
int settings_flat_lock_status = HAL_OK;
int settings_flat_erase_status = HAL_OK;
int settings_flat_program_status = HAL_OK;
unsigned settings_flat_oled_calls = 0;
uint8_t settings_flat_oled_bytes[512]{};
uint8_t settings_flat_oled_commands[512]{};

extern "C" void OLED_WR_Byte(uint8_t data, uint8_t command) {
    if (settings_flat_oled_calls < 512) {
        settings_flat_oled_bytes[settings_flat_oled_calls] = data;
        settings_flat_oled_commands[settings_flat_oled_calls] = command;
    }
    ++settings_flat_oled_calls;
}

class TestHeap final : public NanoUI::MemoryAdapter {
    struct alignas(std::max_align_t) Header { size_t bytes; };
public:
    size_t live = 0, peak = 0, blocks = 0, peakBlocks = 0;
    void* alloc(size_t bytes) override {
        auto* h = static_cast<Header*>(std::malloc(sizeof(Header) + bytes));
        if (!h) std::abort();
        h->bytes = bytes;
        live += bytes;
        ++blocks;
        peak = std::max(peak, live);
        peakBlocks = std::max(peakBlocks, blocks);
        return h + 1;
    }
    void free(void* value) override {
        if (!value) return;
        auto* h = static_cast<Header*>(value) - 1;
        live -= h->bytes;
        --blocks;
        std::free(h);
    }
};

class BitmapCanvas final : public Canvas {
public:
    uint8_t pixels[128 * 64]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        uint8_t& pixel = pixels[y * 128 + x];
        if (color == C_INVERT) pixel ^= 1;
        else pixel = color == C_WHITE;
    }
    bool saveBmp(const std::string& path) const {
        constexpr unsigned scale = 4, w = 128 * scale, h = 64 * scale;
        constexpr unsigned row = w * 3, bytes = 54 + row * h;
        uint8_t header[54]{};
        header[0] = 'B'; header[1] = 'M'; header[10] = 54; header[14] = 40;
        for (int i = 0; i < 4; ++i) {
            header[2 + i] = (bytes >> (8 * i)) & 255;
            header[18 + i] = (w >> (8 * i)) & 255;
            header[22 + i] = (h >> (8 * i)) & 255;
        }
        header[26] = 1; header[28] = 24;
        FILE* file = std::fopen(path.c_str(), "wb");
        if (!file) return false;
        bool ok = std::fwrite(header, 1, sizeof header, file) == sizeof header;
        for (int y = int(h) - 1; y >= 0; --y) for (unsigned x = 0; x < w; ++x) {
            const uint8_t v = pixels[(y / scale) * 128 + x / scale] ? 255 : 0;
            const uint8_t rgb[] = {v, v, v};
            ok = std::fwrite(rgb, 1, sizeof rgb, file) == sizeof rgb && ok;
        }
        return std::fclose(file) == 0 && ok;
    }
};

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static unsigned assertions = 0;
static uint32_t ticks = 0;
static uint32_t getTicks() { return ticks; }

static void send(WindowManager& wm, EventType type) {
    Event event{};
    event.type = type;
    wm.dispatchEvent(event);
}

static void frame(WindowManager& wm, unsigned count = 1) {
    while (count--) { ticks += 16; wm.drawFrame(); }
}

static void checked(bool condition, const char* message) {
    require(condition, message);
    ++assertions;
}

static void snapshot(const BitmapCanvas& canvas, const std::string& dir,
                     const char* name) {
    if (dir.empty()) return;
    checked(canvas.saveBmp(dir + "/" + name + ".bmp"), "save requested BMP snapshot");
}

static RenderObject* focus(WindowManager& wm) { return wm.getFocusedRenderObject(); }

static void moveFocus(WindowManager& wm, unsigned count) {
    while (count--) { send(wm, EVT_DOWN); frame(wm, 24); }
}

static void movePrevious(WindowManager& wm, unsigned count) {
    while (count--) { send(wm, EVT_UP); frame(wm, 24); }
}

int main(int argc, char** argv) {
    const std::string snapshotDir = argc > 1 ? argv[1] : "";
    TestHeap heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(getTicks, nullptr);
    BitmapCanvas canvas;
    WindowManager wm(&canvas, nullptr);
    AppManager::init(&canvas, nullptr);

    // The base App ensures Back follows the existing application-stack policy.
    class Anchor final : public Application {
    public:
        const char* getAppName() override { return "ANCHOR"; }
        Widget* buildUI() override { return new SizedBox(1, 1); }
    };
    sys_cfg.brightness = std::numeric_limits<float>::quiet_NaN();
    sys_cfg.invert_disp = false;
    sys_cfg.screen_off_ms = 4321; // deliberately unlisted
    sys_cfg.deep_sleep_ms = 7777; // deliberately unlisted
    sys_cfg.kill_app_ms = 45678;
    sys_cfg.enable_ble = true;
    sys_cfg.enable_lift_wake = false;
    sys_cfg.enable_tap_wake = true;
    sys_cfg.wake_x_max = 0.73f;
    sys_cfg.wake_y_min = -0.61f;
    sys_cfg.wake_y_max = 0.84f;
    sys_cfg.wake_z_min = 0.29f;
    AppManager::launch(new Anchor());
    const size_t anchorPages = wm.getStackSize();
    AppManager::launch(new SettingsApp());
    require(wm.getStackSize() == anchorPages + 1, "Settings must mount one actual WM page");
    frame(wm, 4);
    snapshot(canvas, snapshotDir, "dirty_initial");

    require(settings_flat_flash_unlock_calls == 0 && settings_flat_flash_erase_calls == 0 &&
            settings_flat_flash_program_calls == 0 && settings_flat_flash_lock_calls == 0,
            "opening Settings must not touch mocked Flash");
    checked(settings_flat_oled_calls == 0, "opening Settings must not preview OLED");
    checked(dynamic_cast<RenderSlider*>(focus(wm)) != nullptr, "focus field 1 is brightness slider");
    RenderSlider* slider = static_cast<RenderSlider*>(focus(wm));
    checked(slider->maxVal == 1.0f && slider->targetWidth == 28, "brightness slider range and width");
    checked(std::fabs(*slider->value - 0.5f) < 0.0001f, "non-finite brightness falls back to 50 percent draft");

    // Edit to the upper bound through real WindowManager key dispatch.
    send(wm, EVT_OK);
    for (int i = 0; i < 10; ++i) send(wm, EVT_RIGHT);
    frame(wm, 4);
    checked(*slider->value == 1.0f, "brightness keyboard edit clamps at 100 percent");
    checked(std::isnan(sys_cfg.brightness), "preview does not copy draft into sys_cfg");
    checked(settings_flat_flash_program_calls == 0, "preview never programs Flash");
    const uint8_t expectedBrightPreview[] = {0x81, 255, 0xD9, 0x22, 0xDB, 0x20};
    checked(settings_flat_oled_calls >= 6 &&
            std::equal(expectedBrightPreview, expectedBrightPreview + 6,
                       settings_flat_oled_bytes + settings_flat_oled_calls - 6),
            "brightness preview retains the existing OLED command sequence");
    snapshot(canvas, snapshotDir, "brightness_editing");
    std::vector<uint8_t> earlyInvertLabel;
    for (int y = 35; y < 43; ++y)
        for (int x = 4; x < 40; ++x) earlyInvertLabel.push_back(canvas.pixels[y * 128 + x]);
    checked(std::count(earlyInvertLabel.begin(), earlyInvertLabel.end(), 1) > 0,
            "non-focused Invert label remains painted during percentage animation");
    frame(wm, 24); // separate stable frame after percent/title slide animations finish
    snapshot(canvas, snapshotDir, "brightness_editing_settled");
    size_t labelPixel = 0;
    bool unchangedLabel = true;
    for (int y = 35; y < 43; ++y)
        for (int x = 4; x < 40; ++x)
            unchangedLabel &= earlyInvertLabel[labelPixel++] == canvas.pixels[y * 128 + x];
    checked(unchangedLabel, "non-focused label pixels do not disappear between early and settled frames");
    for (int i = 0; i < 10; ++i) send(wm, EVT_LEFT);
    checked(*slider->value == 0.0f, "brightness keyboard edit clamps at 0 percent");
    const uint8_t expectedDimPreview[] = {0x81, 0, 0xD9, 0x11, 0xDB, 0x00};
    checked(std::equal(expectedDimPreview, expectedDimPreview + 6,
                       settings_flat_oled_bytes + settings_flat_oled_calls - 6),
            "low brightness preview retains dim OLED command sequence");

    // Back while editing follows the documented global-Back policy and tears
    // down this Settings App; onDestroy restores the currently committed OLED.
    send(wm, EVT_BACK);
    frame(wm, 2);
    checked(wm.getStackSize() == anchorPages, "Back exits Settings while slider editing");
    checked(std::isnan(sys_cfg.brightness), "Back discards draft without changing config");
    checked(settings_flat_oled_calls >= 7, "Back restores OLED after display preview");
    checked(settings_flat_oled_bytes[settings_flat_oled_calls - 1] == 0xA6,
            "Back restores committed non-inverted display");
    checked(settings_flat_flash_erase_calls == 0, "Back does not save");

    // Deep sleep is an enable switch. Editing it remains a draft until APPLY.
    AppManager::launch(new SettingsApp());
    frame(wm, 4);
    checked(wm.getStackSize() == anchorPages + 1, "reopen creates one Settings page");
    moveFocus(wm, 3);
    auto* sleepSwitch = dynamic_cast<RenderSwitch*>(focus(wm));
    checked(sleepSwitch != nullptr && *sleepSwitch->value,
            "focus index 3 is the enabled deep-sleep switch");
    send(wm, EVT_OK);
    checked(!*sleepSwitch->value && sys_cfg.deep_sleep_ms == 7777,
            "deep-sleep edit stays a draft and preserves the committed custom timeout");
    send(wm, EVT_BACK);
    frame(wm, 2);
    checked(wm.getStackSize() == anchorPages && sys_cfg.deep_sleep_ms == 7777,
            "Back discards the deep-sleep enable draft");
    checked(settings_flat_flash_erase_calls == 0 && settings_flat_flash_program_calls == 0,
            "discarding a deep-sleep draft does not save");

    // PAT is now the next focusable control after deep sleep.
    AppManager::launch(new SettingsApp());
    frame(wm, 4);
    moveFocus(wm, 4);
    auto* patSwitch = dynamic_cast<RenderSwitch*>(focus(wm));
    checked(patSwitch != nullptr, "focus index 4 is the PAT input switch");
    checked(CrabitPreferences::enabled(CrabitPreferences::Pat), "PAT starts from its committed default");
    send(wm, EVT_OK);
    checked(!*patSwitch->value && CrabitPreferences::enabled(CrabitPreferences::Pat),
            "PAT edit changes only the draft");
    send(wm, EVT_BACK);
    frame(wm, 2);
    checked(wm.getStackSize() == anchorPages, "Back exits while a PAT draft is active");
    checked(CrabitPreferences::enabled(CrabitPreferences::Pat), "Back discards the PAT draft");
    checked(settings_flat_flash_erase_calls == 0 && settings_flat_flash_program_calls == 0,
            "discarding a PAT draft does not save");

    // Reopen a clean instance. Unknown legacy time values remain exact unless
    // explicitly changed; the sleep value has no focusable picker.
    sys_cfg.brightness = 0.5f;
    AppManager::launch(new SettingsApp());
    frame(wm, 4);
    const std::vector<uint8_t> initialPixels(canvas.pixels, canvas.pixels + sizeof canvas.pixels);
    snapshot(canvas, snapshotDir, "initial");
    checked(dynamic_cast<RenderSlider*>(focus(wm)) != nullptr, "reopened focus starts at brightness");
    moveFocus(wm, 14);
    checked(dynamic_cast<RenderButton*>(focus(wm)) != nullptr, "focus index 14 is APPLY");
    snapshot(canvas, snapshotDir, "actions_bottom");
    checked(std::equal(initialPixels.begin(), initialPixels.begin() + 128 * 12,
                       canvas.pixels), "fixed title/header pixels remain stationary while scrolling");
    movePrevious(wm, 12); // APPLY -> face/BLE/inputs/deep sleep -> screen off
    checked(dynamic_cast<RenderSettingsOptionPicker*>(focus(wm)) != nullptr,
            "focus index 2 is the screen-off picker");
    send(wm, EVT_OK);
    send(wm, EVT_RIGHT); // CUSTOM -> first listed choice (3s)
    frame(wm, 32); // allow picker and focus-reveal animation to settle for the captured frame
    snapshot(canvas, snapshotDir, "off_editing");
    send(wm, EVT_OK);
    movePrevious(wm, 1); // screen off -> invert
    checked(dynamic_cast<RenderSwitch*>(focus(wm)) != nullptr, "focus index 1 is the invert switch");
    send(wm, EVT_OK);
    checked(settings_flat_oled_bytes[settings_flat_oled_calls - 1] == 0xA7,
            "invert preview uses existing OLED command");
    checked(!sys_cfg.invert_disp, "invert draft is not committed before APPLY");
    moveFocus(wm, 3); // invert -> screen off -> deep sleep -> PAT
    patSwitch = dynamic_cast<RenderSwitch*>(focus(wm));
    checked(patSwitch != nullptr, "focus index 4 is the PAT switch after deep sleep");
    checked(sys_cfg.deep_sleep_ms == 7777, "legacy deep-sleep value remains untouched while navigating");
    send(wm, EVT_OK);
    checked(!*patSwitch->value && CrabitPreferences::enabled(CrabitPreferences::Pat),
            "PAT stays a draft until Apply");
    snapshot(canvas, snapshotDir, "input_editing");
    moveFocus(wm, 10); // PAT -> QMI -> lift -> tap -> sound -> volume -> LED -> level -> BLE -> face -> APPLY
    checked(dynamic_cast<RenderButton*>(focus(wm)) != nullptr, "focus index 14 is APPLY after the input rows");
    checked(wm.getStackSize() == anchorPages + 1, "toast does not push a WM page");
    const SystemConfig unrelatedBeforeApply = sys_cfg;
    // Program word 7 is the preference trailer. A failed trailer write must be
    // reported and Flash re-locked; the unchanged Apply path must remain retryable.
    settings_flat_program_fail_call = settings_flat_flash_program_calls + 7;
    send(wm, EVT_OK);
    frame(wm, 2);
    checked(std::strcmp(Popup::toastMsg, "保存失败") == 0,
            "failed trailer program reports ConfigManager failure");
    checked(settings_flat_flash_unlock_calls == 1 && settings_flat_flash_erase_calls == 1 &&
            settings_flat_flash_program_calls == 7 && settings_flat_flash_lock_calls == 1,
            "trailer program failure still reaches the Flash lock path");
    CrabitPreferences::Record failed_trailer{};
    std::memcpy(&failed_trailer, settings_flat_flash_image + sizeof(SystemConfig),
                sizeof(failed_trailer));
    checked(!CrabitPreferences::valid(failed_trailer), "failed trailer is not accepted as a saved record");
    checked(std::memcmp(settings_flat_flash_image, &sys_cfg, sizeof(SystemConfig)) == 0,
            "failed trailer does not change the legacy 48-byte config image");

    settings_flat_program_fail_call = 0;
    send(wm, EVT_OK);
    frame(wm, 2);
    checked(std::strcmp(Popup::toastMsg, "已应用") == 0, "retry toast confirms successful save");
    checked(settings_flat_flash_unlock_calls == 2 && settings_flat_flash_erase_calls == 2 &&
            settings_flat_flash_program_calls == 14 && settings_flat_flash_lock_calls == 2,
            "retry rewrites six legacy words and one preference trailer word");
    checked(sys_cfg.screen_off_ms == 3000 && sys_cfg.deep_sleep_ms == 7777,
            "apply changes selected timeout and preserves unknown sleep value");
    checked(sys_cfg.invert_disp && sys_cfg.brightness == 0.5f,
            "apply commits only the intended display drafts");
    checked(!CrabitPreferences::enabled(CrabitPreferences::Pat) &&
            CrabitPreferences::enabled(CrabitPreferences::Qmi) &&
            CrabitPreferences::enabled(CrabitPreferences::Sound) &&
            CrabitPreferences::enabled(CrabitPreferences::Led),
            "Apply commits the PAT draft without changing other input preferences");
    checked(GlobalStore.sys_volume == CrabitPreferences::current.volume,
            "Apply synchronizes the committed preference volume to GlobalStore");
    CrabitPreferences::Record saved_trailer{};
    std::memcpy(&saved_trailer, settings_flat_flash_image + sizeof(SystemConfig),
                sizeof(saved_trailer));
    const CrabitPreferences::Record expected_trailer = CrabitPreferences::sealed();
    checked(CrabitPreferences::valid(saved_trailer), "saved preference trailer has a valid CRC and magic");
    checked(std::memcmp(settings_flat_flash_image, &sys_cfg, sizeof(SystemConfig)) == 0,
            "the optional trailer leaves all 48 legacy config bytes intact");
    checked(std::memcmp(&saved_trailer, &expected_trailer, sizeof(saved_trailer)) == 0,
            "the saved trailer matches the committed preference record");
    checked(sys_cfg.kill_app_ms == unrelatedBeforeApply.kill_app_ms &&
            sys_cfg.enable_ble == unrelatedBeforeApply.enable_ble &&
            sys_cfg.enable_lift_wake == unrelatedBeforeApply.enable_lift_wake &&
            sys_cfg.enable_tap_wake == unrelatedBeforeApply.enable_tap_wake &&
            sys_cfg.wake_x_max == unrelatedBeforeApply.wake_x_max &&
            sys_cfg.wake_y_min == unrelatedBeforeApply.wake_y_min &&
            sys_cfg.wake_y_max == unrelatedBeforeApply.wake_y_max &&
            sys_cfg.wake_z_min == unrelatedBeforeApply.wake_z_min,
            "apply preserves unrelated BLE, wake and kill-app fields");
    checked(wm.getStackSize() == anchorPages + 1, "APPLIED toast stays on current page");

    // A second Apply with no changes reports NO CHANGES and does not save.
    checked(dynamic_cast<RenderButton*>(focus(wm)) != nullptr, "focus remains on APPLY");
    send(wm, EVT_OK);
    checked(std::strcmp(Popup::toastMsg, "没有修改") == 0, "unchanged Apply reports no changes");
    checked(settings_flat_flash_unlock_calls == 2 && settings_flat_flash_erase_calls == 2 &&
            settings_flat_flash_program_calls == 14 && settings_flat_flash_lock_calls == 2,
            "unchanged Apply does not save");
    frame(wm, 100); // advance actual toast clock past its 1500 ms lifetime
    checked(Popup::toastMsg == nullptr, "actual toast expires after mocked UI ticks advance");

    send(wm, EVT_BACK);
    frame(wm, 2);
    checked(wm.getStackSize() == anchorPages, "Back exits applied Settings page");

    // A fresh page changes only invert while both timeouts are unknown. Both
    // CUSTOM values must survive this unrelated apply byte-for-byte.
    sys_cfg.screen_off_ms = 4321;
    sys_cfg.deep_sleep_ms = 7777;
    AppManager::launch(new SettingsApp());
    frame(wm, 3);
    moveFocus(wm, 1);
    checked(dynamic_cast<RenderSwitch*>(focus(wm)) != nullptr, "reopened committed page reaches invert second");
    send(wm, EVT_OK); // true -> false draft
    moveFocus(wm, 13); // invert -> screen-off -> deep sleep/PAT/QMI/lift/tap/sound/volume/LED/level/BLE/face -> APPLY
    checked(dynamic_cast<RenderButton*>(focus(wm)) != nullptr, "reopened page reaches focus index 14 APPLY");
    send(wm, EVT_OK);
    frame(wm, 2);
    checked(sys_cfg.screen_off_ms == 4321 && sys_cfg.deep_sleep_ms == 7777,
            "editing invert and applying preserves both unknown timeout values");
    checked(!sys_cfg.invert_disp && settings_flat_flash_unlock_calls == 3 &&
            settings_flat_flash_program_calls == 21,
            "second changed Apply commits invert through one more save");
    movePrevious(wm, 13); // APPLY -> invert
    send(wm, EVT_OK); // new preview after the successful Apply
    checked(sys_cfg.invert_disp == false && settings_flat_oled_bytes[settings_flat_oled_calls - 1] == 0xA7,
            "post-Apply invert preview remains a draft");
    send(wm, EVT_BACK);
    frame(wm, 2);
    checked(wm.getStackSize() == anchorPages, "Back exits second Settings session");
    checked(settings_flat_oled_bytes[settings_flat_oled_calls - 1] == 0xA6,
            "Back after Apply restores the latest committed non-inverted OLED state");

    // Existing zero values survive an unrelated invert-only Apply; legacy
    // deep sleep remains read-only and is never a focus destination.
    sys_cfg.screen_off_ms = 0;
    sys_cfg.deep_sleep_ms = 0;
    AppManager::launch(new SettingsApp());
    frame(wm, 3);
    moveFocus(wm, 1); // invert
    send(wm, EVT_OK);
    moveFocus(wm, 13); // invert -> ... -> APPLY
    send(wm, EVT_OK);
    checked(sys_cfg.screen_off_ms == 0 && sys_cfg.deep_sleep_ms == 0,
            "unrelated invert Apply preserves both stored zero timeout values");
    checked(settings_flat_flash_unlock_calls == 4 && settings_flat_flash_program_calls == 28,
            "zero-preserving invert Apply makes one save");
    send(wm, EVT_BACK);
    frame(wm, 2);

    // Screen-off zero is a stored CUSTOM value, never an offered option. The
    // disabled deep-sleep switch remains unchanged during this screen-off edit.
    AppManager::launch(new SettingsApp());
    frame(wm, 3);
    moveFocus(wm, 2); // screen-off
    send(wm, EVT_OK);
    send(wm, EVT_RIGHT);
    send(wm, EVT_OK);
    moveFocus(wm, 1); // screen-off -> deep-sleep enable
    auto* deepSleepSwitch = dynamic_cast<RenderSwitch*>(focus(wm));
    checked(deepSleepSwitch != nullptr && !*deepSleepSwitch->value,
            "focus index 3 is the disabled deep-sleep switch");
    checked(sys_cfg.deep_sleep_ms == 0, "disabled deep sleep remains zero while navigating");
    moveFocus(wm, 1); // deep sleep -> PAT
    checked(dynamic_cast<RenderSwitch*>(focus(wm)) != nullptr, "focus index 4 is PAT");
    moveFocus(wm, 10); // PAT -> QMI/lift/tap/sound/volume/LED/level/BLE/face/APPLY
    checked(dynamic_cast<RenderButton*>(focus(wm)) != nullptr, "zero-valued screen-off reaches APPLY");
    send(wm, EVT_OK);
    checked(sys_cfg.screen_off_ms == 3000,
            "screen-off zero is retained as CUSTOM and is not offered as NEVER");
    checked(sys_cfg.deep_sleep_ms == 0,
            "screen-off Apply preserves the legacy deep-sleep value without a picker");
    checked(settings_flat_flash_unlock_calls == 5 && settings_flat_flash_program_calls == 35,
            "time option apply makes one additional save");
    send(wm, EVT_BACK);
    frame(wm, 2);
    checked(wm.getStackSize() == anchorPages, "Back exits zero-semantics test session");

    // Reopening after Apply loads committed values into the real controls.
    AppManager::launch(new SettingsApp());
    frame(wm, 3);
    checked(std::fabs(*static_cast<RenderSlider*>(focus(wm))->value - 0.5f) < 0.0001f,
            "reopened slider reflects applied brightness");
    moveFocus(wm, 1);
    checked(*static_cast<RenderSwitch*>(focus(wm))->value,
            "reopened invert switch reflects applied value");
    moveFocus(wm, 13);
    checked(dynamic_cast<RenderButton*>(focus(wm)) != nullptr, "focus reaches APPLY from invert");
    moveFocus(wm, 1);
    checked(dynamic_cast<RenderButton*>(focus(wm)) != nullptr, "BACK follows APPLY in the action row");
    send(wm, EVT_OK); // Exercise the actual BACK button callback, not only global Back.
    frame(wm, 2);
    checked(wm.getStackSize() == anchorPages, "final Back returns to anchor page");
    AppManager::launch(new SettingsApp());
    frame(wm, 3);
    const auto saves_before_ble = settings_flat_flash_unlock_calls;
    moveFocus(wm, 12); // Bluetooth immediate action, separate from APPLY.
    send(wm, EVT_OK);
    checked(Crabit::CompanionBle::toggle_calls == 1 && Crabit::CompanionBle::active(),
            "Settings Bluetooth uses shared session toggle immediately");
    AppManager::tick();
    frame(wm, 120); // Let the STARTING toast expire before visual inspection.
    moveFocus(wm, 1); // Reveal the status line above APPLY in the scroll viewport.
    snapshot(canvas, snapshotDir, "bluetooth_on");
    movePrevious(wm, 1);
    checked(settings_flat_flash_unlock_calls == saves_before_ble && sys_cfg.enable_ble,
            "Bluetooth action never persists or changes legacy boot preference");
    Crabit::CompanionBle::fake_transitioning = true;
    send(wm, EVT_OK);
    checked(Crabit::CompanionBle::toggle_calls == 1, "transitioning session rejects repeated UI toggle");
    Crabit::CompanionBle::fake_transitioning = false;
    send(wm, EVT_OK);
    checked(Crabit::CompanionBle::toggle_calls == 2 && !Crabit::CompanionBle::active(),
            "Settings can stop the active session through same owner");
    send(wm, EVT_BACK);
    frame(wm, 2);
    // Old config tail padding cannot become a random watch-face preference.
    uint8_t old_fields[42];
    std::memcpy(old_fields, &sys_cfg, sizeof(old_fields));
    std::memset(sys_cfg.watch_face_tag, 0xCC, sizeof(sys_cfg.watch_face_tag));
    sys_cfg.watch_face = 2;
    ConfigManager::normalizeWatchFace();
    checked(sys_cfg.watch_face == 0, "legacy padding defaults to classic face");
    checked(std::memcmp(old_fields, &sys_cfg, sizeof(old_fields)) == 0,
            "legacy face normalization preserves every old config field byte");
    const auto before_face_save = settings_flat_flash_unlock_calls;
    AppManager::launch(new SettingsApp());
    frame(wm, 3);
    moveFocus(wm, 13);
    checked(dynamic_cast<RenderSettingsOptionPicker*>(focus(wm)) != nullptr,
            "focus index 13 is the face picker");
    send(wm, EVT_OK); send(wm, EVT_LEFT); send(wm, EVT_OK);
    checked(sys_cfg.watch_face == 0, "face edit remains a draft");
    send(wm, EVT_BACK);
    checked(settings_flat_flash_unlock_calls == before_face_save,
            "discarding a face draft never saves");
    AppManager::launch(new SettingsApp());
    frame(wm, 3);
    moveFocus(wm, 13);
    send(wm, EVT_OK); send(wm, EVT_LEFT); send(wm, EVT_OK);
    moveFocus(wm, 1); send(wm, EVT_OK);
    checked(sys_cfg.watch_face == Crabit::watchFaceCount - 1 &&
            settings_flat_flash_unlock_calls == before_face_save + 1 &&
            settings_flat_flash_program_calls == 42,
            "face Apply writes through existing config save exactly once");
    SystemConfig committed = sys_cfg;
    sys_cfg = SystemConfig{};
    std::memcpy(&sys_cfg, settings_flat_flash_image, sizeof(sys_cfg));
    ConfigManager::normalizeWatchFace();
    checked(sys_cfg.watch_face == Crabit::watchFaceCount - 1 && std::memcmp(&committed, &sys_cfg, sizeof(sys_cfg)) == 0,
            "face preference survives mocked Flash save/read and normalization");
    sys_cfg.watch_face = 255;
    ConfigManager::normalizeWatchFace();
    checked(sys_cfg.watch_face == 0, "invalid stored face falls back to classic");
    send(wm, EVT_BACK);
    const size_t anchorLive = heap.live;
    for (unsigned cycle = 0; cycle < 30; ++cycle) {
        AppManager::launch(new SettingsApp());
        frame(wm, 4);
        AppManager::exitCurrentApp();
        frame(wm, 1);
        // An earlier save Toast may expire during these frames and release memory.
        require(heap.live <= anchorLive, "repeated Settings visits do not retain UI allocations");
    }
    AppManager::shutdown();
    require(heap.live == 0 && heap.blocks == 0, "UI allocations released after shutdown");
    std::printf("settings-memory: peak=%zu peak-blocks=%zu live=%zu\n",
                heap.peak, heap.peakBlocks, heap.live);
    checked(settings_flat_flash_program_calls > 0, "ConfigManager spy observed real save path");
    std::printf("settings-flat: %u runtime assertions passed; source page rendered through shared NanoUI\n", assertions);
    return 0;
}
