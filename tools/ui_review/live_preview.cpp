// Native host window around real product Apps. Board services below are fakes.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <NanoUI.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "app_clock.h"
#include "app_settings.h"
#include "app_time_set.h"
#include "app_sysinfo.h"
#include "app_bw16_flash.h"
#include "product_ui_pool.h"

class UnavailableDeviceApp : public Application {
   public:
    const char* getAppName() override { return "OFFLINE"; }
    Widget* buildUI() override { return new Text("此离线预览不运行硬件功能", 8); }
};
using SensorApp = UnavailableDeviceApp;
using PATApp = UnavailableDeviceApp;
using BMP280App = UnavailableDeviceApp;
using AXP2101App = UnavailableDeviceApp;
using WeatherApp = UnavailableDeviceApp;
using AppAudioSettings = UnavailableDeviceApp;
using CyberLinkApp = UnavailableDeviceApp;
#include "launcher_preview.h"

namespace {
uint32_t ticks = 1000, extraSeconds = 0;
uint32_t getTicks() {
    return ticks;
}
std::wstring preferencePath;
unsigned saves = 0;
unsigned rtcWrites = 0, bridgeCalls = 0;
RTC_TimeTypeDef writtenTime{};
RTC_DateTypeDef writtenDate{};
class Heap : public FreeRTOSMemoryAdapter {
   public:
    void* alloc(size_t bytes) override {
        void* pointer = FreeRTOSMemoryAdapter::alloc(bytes);
        if (!pointer) {
            if (FILE* report = std::fopen("self-test-error.txt", "w")) {
                std::fprintf(report, "Product 12KiB UI pool exhausted requesting %zu bytes\n",
                             bytes);
                std::fclose(report);
            }
            std::fprintf(stderr, "Product 12KiB UI pool exhausted requesting %zu bytes\n", bytes);
            std::exit(4);
        }
        return pointer;
    }
};
class PreviewCanvas : public Canvas {
   public:
    std::array<uint32_t, 128 * 64> pixels{};
    unsigned displays = 0;
    void clear() override { pixels.fill(0); }
    void display() override { ++displays; }
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64)
            return;
        auto& pixel = pixels[y * 128 + x];
        if (color == C_INVERT)
            pixel ^= 0xFFFFFF;
        else
            pixel = color == C_WHITE ? 0xFFFFFF : 0;
    }
    void save(const std::string& path) {
        FILE* file = std::fopen(path.c_str(), "wb");
        if (!file)
            std::exit(2);
        std::fprintf(file, "P5\n128 64\n255\n");
        for (auto pixel : pixels)
            std::fputc(pixel ? 255 : 0, file);
        std::fclose(file);
    }
};
PreviewCanvas* canvas;
WindowManager* manager;
void frame(unsigned count = 1) {
    while (count--) {
        ticks += 33;
        if (fakeSensorAutoUpdate && fakeSensorAvailable) {
            fakeSensor.published_at = ticks;
            fakeSensor.battery.timestamp = ticks;
            fakeSensor.pressure.timestamp = ticks;
        }
        AppManager::tick();
        manager->drawFrame();
    }
}
void send(EventType type, unsigned count = 1) {
    while (count--) {
        manager->dispatchEvent({type, 0});
        frame(25);
    }
}
void loadPreferences() {
    FILE* file = _wfopen(preferencePath.c_str(), L"rb");
    if (!file)
        return;
    float brightness;
    unsigned invert, off, sleep, face;
    if (std::fscanf(file, "%f %u %u %u %u", &brightness, &invert, &off, &sleep, &face) == 5 &&
        std::isfinite(brightness) && brightness >= 0 && brightness <= 1 && invert <= 1 &&
        off <= 600000 && sleep <= 600000 && face < Crabit::watchFaceCount) {
        sys_cfg = {brightness, invert != 0, off, sleep, static_cast<uint8_t>(face)};
    }
    std::fclose(file);
}
void require(bool condition, const char* message) {
    if (!condition) {
        if (FILE* report = std::fopen("self-test-error.txt", "w")) {
            std::fprintf(report, "%s\n", message);
            std::fclose(report);
        }
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(3);
    }
}
std::vector<std::string> clockTexts;
void inspectClock(RenderObject* object) {
    if (auto* flex = dynamic_cast<RenderFlex*>(object))
        require(flex->overflowMainExtent == 0, "classic clock layout has no flex overflow");
    if (auto* text = dynamic_cast<RenderSlidingText*>(object)) {
        if (get_utf8_string_width(text->dataSource->c_str(), 8) > text->size.width)
            std::fprintf(stderr, "Clock text clipped: %s width=%d required=%d\n",
                         text->dataSource->c_str(), text->size.width,
                         get_utf8_string_width(text->dataSource->c_str(), 8));
        require(text->fontSize == 8 && text->size.height >= 8 &&
                    get_utf8_string_width(text->dataSource->c_str(), 8) <= text->size.width,
                "classic status text fits its 8px slot");
        clockTexts.emplace_back(text->dataSource->c_str());
        const char* p = text->dataSource->c_str();
        while (*p) {
            uint32_t code = decode_utf8(&p);
            uint8_t width = 0;
            if (code >= 128)
                require(get_chinese_glyph(code, 8, &width, 0) != nullptr,
                        "classic Chinese status glyph is present");
        }
    }
    object->visitChildren(inspectClock);
}
void checkClockStatus(const std::string& output, const char* name,
                      std::initializer_list<const char*> expected) {
    ClockApp app;
    app.onCreate();
    Widget* widget = app.buildUI();
    RenderObject* object = widget->createRenderObject();
    object->layout(BoxConstraints::tight(128, 64));
    clockTexts.clear();
    inspectClock(object);
    for (const char* value : expected)
        require(std::find(clockTexts.begin(), clockTexts.end(), value) != clockTexts.end(),
                "classic binds the expected sensor/cache state");
    PreviewCanvas sample;
    sample.resetClipRect();
    object->paint(&sample, {0, 0});
    sample.save(output + "/" + name + ".pgm");
    delete object;
    delete widget;
}
int selfTest(const std::string& output) {
    if (FILE* report = std::fopen("self-test-error.txt", "w"))
        std::fclose(report);
    sys_cfg = {};
    frame(60);
    canvas->save(output + "/product-classic.pgm");
    const auto classic = canvas->pixels;
    // Simulated inputs below exercise the actual ClockApp formatting/bindings,
    // not a second implementation of its status logic.
    checkClockStatus(output, "classic-ready", {"76%", "天气 晴 18~26C", "手表温度24.6C", "蓝牙关"});
    Crabit::CompanionBle::fakeActive = true;
    Crabit::CompanionBle::fakeStatus = "ADVERTISING";
    checkClockStatus(output, "classic-ble-on", {"蓝牙开"});
    Crabit::CompanionBle::fakeStatus = "CONNECTED";
    checkClockStatus(output, "classic-ble-connected", {"蓝牙已连"});
    Crabit::CompanionBle::fakeActive = false;
    Crabit::CompanionBle::fakeStatus = "OFF";
    const auto savedSensor = fakeSensor;
    fakeSensorAvailable = false;
    Crabit::NetworkWeather::fakeCount = 0;
    checkClockStatus(output, "classic-unavailable", {"--%", "天气未同步", "手表温度--"});
    fakeSensorAvailable = true;
    fakeSensor.battery.percent = 101;
    fakeSensor.pressure.timestamp = ticks - 30001u;
    Crabit::NetworkWeather::fakeCount = 1;
    Crabit::NetworkWeather::fakeDays[0].date = 20261021;
    checkClockStatus(output, "classic-stale", {"--%", "缓存天气 晴 18~26C", "手表温度--"});
    fakeSensor = savedSensor;
    fakeSensor.battery.is_charging = true;
    fakeSensor.pressure.temperature = -0.5f;
    Crabit::NetworkWeather::fakeCount = 2;
    Crabit::NetworkWeather::fakeDays[1] = {20261022, 2, 100, 210, 10};
    checkClockStatus(output, "classic-charging", {"充76%", "天气 多云 10~21C", "手表温度-0.5C"});
    fakeSensor = savedSensor;
    Crabit::NetworkWeather::fakeCount = 1;
    Crabit::NetworkWeather::fakeDays[0].date = 20261022;
    // Walk real product menu callbacks instead of testing only a direct launch.
    send(EVT_OK);
    require(manager->getStackSize() == 2, "clock OK opens real launcher");
    canvas->save(output + "/product-main-menu.pgm");
    send(EVT_RIGHT, 2);
    send(EVT_OK);
    require(manager->getStackSize() == 3, "launcher opens real system folder");
    canvas->save(output + "/product-system-folder.pgm");
    send(EVT_DOWN, 2);
    send(EVT_OK);
    require(manager->getStackSize() == 4, "system folder opens Settings through real callback");
    canvas->save(output + "/product-settings-entry.pgm");
    send(EVT_BACK, 3);
    for (unsigned face = 1; face < Crabit::watchFaceCount; ++face) {
        AppManager::launch(new SettingsApp());
        frame(30);
        send(EVT_DOWN, 5);  // Actual face picker, not a direct config assignment.
        require(dynamic_cast<RenderSettingsOptionPicker*>(manager->getFocusedRenderObject()),
                "Settings focus reaches watch face picker");
        send(EVT_OK);
        send(EVT_RIGHT);
        send(EVT_OK);
        require(sys_cfg.watch_face == face - 1, "draft does not alter saved face");
        canvas->save(output + "/product-picker-" + std::to_string(face) + ".pgm");
        send(EVT_DOWN);
        send(EVT_OK);
        require(sys_cfg.watch_face == face && saves == face, "Apply saves selected face once");
        sys_cfg.watch_face = 0;
        loadPreferences();
        require(sys_cfg.watch_face == face, "desktop preferences survive reload");
        send(EVT_BACK);
        frame(60);
        RenderObject* mountedRoot = manager->getFocusedRenderObject();
        while (mountedRoot->parent)
            mountedRoot = mountedRoot->parent;
        clockTexts.clear();
        inspectClock(mountedRoot);
        if (face >= 3) {
            for (unsigned y = 0; y < 10; ++y) {
                require(std::equal(classic.begin() + y * 128, classic.begin() + y * 128 + 45,
                                   canvas->pixels.begin() + y * 128),
                        "mounted face preserves the complete date pixels");
                require(std::equal(classic.begin() + y * 128 + 100, classic.begin() + y * 128 + 128,
                                   canvas->pixels.begin() + y * 128 + 100),
                        "mounted face preserves the complete battery pixels");
            }
        }
        canvas->save(output + "/product-face-" + std::to_string(face) + ".pgm");
        require(canvas->pixels != classic, "return rebuilds selected time face");
        const unsigned before = canvas->displays;
        frame(8);
        if (face <= 2)
            require(canvas->displays == before, "time-only face stops rendering at rest");
        else {
            auto* button = dynamic_cast<RenderButton*>(manager->getFocusedRenderObject());
            require(button && button->getRadius() == 2,
                    "menu button forwards rounded radius to native focus painter");
            auto* surface = dynamic_cast<RenderDecoratedBox*>(button->child);
            require(surface && !surface->hasBorder,
                    "menu has no extra painted background or border beneath native focus");
            Point origin{0, 0};
            for (RenderObject* node = button; node; node = node->parent) {
                origin.x += node->pos.x;
                origin.y += node->pos.y;
            }
            for (int x = 2; x < button->size.width - 2; ++x)
                require(canvas->pixels[(origin.y + 1) * 128 + origin.x + x] != 0,
                        "focused menu has a solid white interior, not a double-inverted frame");
        }
        const auto endpoint = canvas->pixels;
        extraSeconds += 60;
        frame(17);  // Poll RTC; transition may be in progress.
        frame(20);
        require(canvas->pixels != endpoint, "next minute updates the time face");
        const unsigned settled = canvas->displays;
        frame(8);
        if (face <= 2)
            require(canvas->displays == settled, "minute animation settles");
        send(EVT_OK);
        require(manager->getStackSize() == 2, "time-only OK opens product launcher");
        send(EVT_BACK);
    }
    AppManager::launch(new SettingsApp());
    frame(30);
    send(EVT_DOWN, 5);
    send(EVT_OK);
    send(EVT_RIGHT);
    send(EVT_OK);
    send(EVT_BACK);
    require(sys_cfg.watch_face == Crabit::watchFaceCount - 1 && saves == Crabit::watchFaceCount - 1,
            "Back cancels an unapplied face draft");
    auto usage = []() {
        auto& pool = NanoUI::MemoryPool::instance();
        return pool.getWidgetUsage() + pool.getRenderUsage() + pool.getVectorUsage();
    };
    const size_t beforeNavigation = usage();
    for (unsigned visit = 0; visit < 20; ++visit) {
        send(EVT_OK);
        send(EVT_RIGHT, 2);
        send(EVT_OK);
        send(EVT_DOWN, 2);
        send(EVT_OK);
        require(manager->getStackSize() == 4, "repeated real-menu Settings entry succeeds");
        send(EVT_BACK, 3);
        require(manager->getStackSize() == 1 && usage() == beforeNavigation,
                "menu/Settings round trip returns pooled UI bytes to baseline");
    }
    AppManager::launch(new TimeSettingApp());
    frame(30);
    canvas->save(output + "/product-time-settings.pgm");
    auto* yearPicker = dynamic_cast<RenderNumberPicker*>(manager->getFocusedRenderObject());
    require(yearPicker && *yearPicker->value == 26, "time page reads real RTC year");
    send(EVT_OK);
    send(EVT_RIGHT);
    send(EVT_OK);
    require(*yearPicker->value == 27 && rtcWrites == 0, "time picker changes draft only");
    send(EVT_DOWN, 5);
    send(EVT_OK);
    require(rtcWrites == 2 && writtenDate.Year == 27 && writtenDate.Month == 10 &&
                writtenDate.Date == 22 && writtenDate.WeekDay == 5 && writtenTime.Seconds == 0,
            "save applies edited time/date with computed weekday");
    send(EVT_BACK);
    require(usage() == beforeNavigation, "time page releases pooled UI bytes");
    AppManager::launch(new TimeSettingApp());
    frame(30);
    send(EVT_DOWN);
    auto* monthPicker = dynamic_cast<RenderNumberPicker*>(manager->getFocusedRenderObject());
    require(monthPicker != nullptr, "month has a real numeric picker");
    *monthPicker->value = 2;
    send(EVT_DOWN);
    auto* dayPicker = dynamic_cast<RenderNumberPicker*>(manager->getFocusedRenderObject());
    require(dayPicker != nullptr, "day has a real numeric picker");
    *dayPicker->value = 31;
    send(EVT_DOWN, 3);
    send(EVT_OK);
    require(rtcWrites == 2, "invalid calendar date never writes RTC");
    send(EVT_BACK);
    AppManager::launch(new SysInfoApp());
    frame(60);
    canvas->save(output + "/product-system-info.pgm");
    send(EVT_DOWN, 3);
    canvas->save(output + "/product-system-info-detail.pgm");
    send(EVT_BACK);
    require(usage() == beforeNavigation, "system info releases pooled UI bytes");
    AppManager::launch(new FlashApp());
    frame(60);
    canvas->save(output + "/product-flash.pgm");
    require(bridgeCalls == 0, "opening flash page never starts bridge");
    send(EVT_OK);
    require(bridgeCalls == 1, "explicit flash confirmation retains bridge entry");
    send(EVT_BACK);
    require(usage() == beforeNavigation, "flash page releases pooled UI bytes");
    Crabit::WatchFaces::TimeTransition transition;
    transition.set(23, 59, 0xFFFFFFF0u, false);
    transition.set(0, 0, 0xFFFFFFF0u);
    require(transition.phase(0x20u) > 0 && transition.phase(0x20u) < 1,
            "animation elapsed time tolerates tick wrap");
    require(transition.phase(1000) == 1, "transition ends after budget");
    std::puts(
        "Live product clock/settings: switch, cancel, save/reload, OK, idle and minute tests PASS");
    return 0;
}
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_CREATE:
            CreateWindowW(L"BUTTON", L"打开菜单", WS_CHILD | WS_VISIBLE, 16, 16, 120, 30, window,
                          (HMENU)1, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"下一分钟", WS_CHILD | WS_VISIBLE, 148, 16, 120, 30, window,
                          (HMENU)2, nullptr, nullptr);
            SetTimer(window, 1, 33, nullptr);
            return 0;
        case WM_TIMER: {
            unsigned before = canvas->displays;
            frame();
            if (canvas->displays != before)
                InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == 1 && manager->getStackSize() == 1)
                AppManager::launch(createLauncherApp());
            if (LOWORD(w) == 2)
                extraSeconds += 60;
            SetFocus(window);
            return 0;
        case WM_KEYDOWN: {
            EventType event = EVT_NONE;
            if (w == VK_UP)
                event = EVT_UP;
            if (w == VK_DOWN)
                event = EVT_DOWN;
            if (w == VK_LEFT)
                event = EVT_LEFT;
            if (w == VK_RIGHT)
                event = EVT_RIGHT;
            if (w == VK_RETURN)
                event = EVT_OK;
            if (w == VK_ESCAPE)
                event = EVT_BACK;
            if (event != EVT_NONE)
                manager->dispatchEvent({event, 0});
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = 128;
            info.bmiHeader.biHeight = -64;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, 16, 70, 512, 256, 0, 0, 128, 64, canvas->pixels.data(), &info,
                          DIB_RGB_COLORS, SRCCOPY);
            StretchDIBits(dc, 560, 70, 128, 64, 0, 0, 128, 64, canvas->pixels.data(), &info,
                          DIB_RGB_COLORS, SRCCOPY);
            RECT note{16, 350, 800, 440};
            DrawTextW(
                dc,
                L"真实产品 ClockApp / SettingsApp · 离线服务\n"
                L"上下选择，Enter 确认，左右修改，Esc 返回。表盘修改后选择“应用”，再返回。\n"
                L"时间、电量、天气、手表温度均为模拟；蓝牙不可用。桌面保存不代表手表 Flash 验收。",
                -1, &note, DT_WORDBREAK);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(window, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
}  // namespace

ConfigManager::SaveResult ConfigManager::save() {
    FILE* file = _wfopen(preferencePath.c_str(), L"wb");
    if (!file)
        return SaveResult::ProgramFailed;
    bool ok = std::fprintf(file, "%.3f %u %u %u %u\n", double(sys_cfg.brightness),
                           unsigned(sys_cfg.invert_disp), unsigned(sys_cfg.screen_off_ms),
                           unsigned(sys_cfg.deep_sleep_ms), unsigned(sys_cfg.watch_face)) > 0;
    ok = std::fclose(file) == 0 && ok;
    if (ok)
        ++saves;
    return ok ? SaveResult::Saved : SaveResult::ProgramFailed;
}
int HAL_RTC_GetTime(int*, RTC_TimeTypeDef* value, int) {
    const uint32_t seconds = (12 * 3600 + 59 * 60 + ticks / 1000 + extraSeconds) % 86400;
    *value = {uint8_t(seconds / 3600), uint8_t(seconds / 60 % 60), uint8_t(seconds % 60)};
    return HAL_OK;
}
int HAL_RTC_GetDate(int*, RTC_DateTypeDef* value, int) {
    *value = {10, 22, 4, 26};
    return HAL_OK;
}
extern "C" void OLED_WR_Byte(uint8_t, uint8_t) {
}
GlobalDataStore GlobalStore;
extern "C" void BW16_Dedicated_Bridge() {
    ++bridgeCalls;
}
int HAL_RTC_SetTime(int*, RTC_TimeTypeDef* value, int) {
    writtenTime = *value;
    ++rtcWrites;
    return HAL_OK;
}
int HAL_RTC_SetDate(int*, RTC_DateTypeDef* value, int) {
    writtenDate = *value;
    ++rtcWrites;
    return HAL_OK;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR arguments, int show) {
    wchar_t executable[MAX_PATH];
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring directory(executable);
    directory.resize(directory.find_last_of(L"\\/") + 1);
    const bool test = std::string(arguments) == "--self-test";
    preferencePath = directory + (test ? L"self-test-preferences.txt" : L"ui-preferences.txt");
    if (!test)
        loadPreferences();
    if (std::string(arguments) == "--classic")
        sys_cfg.watch_face = 0;
    else if (std::string(arguments) == "--split")
        sys_cfg.watch_face = static_cast<uint8_t>(Crabit::WatchFace::Split);
    else if (std::string(arguments) == "--stacked")
        sys_cfg.watch_face = static_cast<uint8_t>(Crabit::WatchFace::Stacked);
    Heap heap;
    heap.init();
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(getTicks, nullptr);
    fakeSensor.battery.is_valid = true;
    fakeSensor.battery.percent = 76;
    fakeSensor.battery.timestamp = ticks;
    fakeSensor.pressure.is_valid = true;
    fakeSensor.pressure.temperature = 24.6f;
    fakeSensor.pressure.timestamp = ticks;
    PreviewCanvas surface;
    WindowManager wm(&surface, nullptr);
    canvas = &surface;
    manager = &wm;
    AppManager::init(canvas, nullptr);
    AppManager::launch(new ClockApp());
    int result = 0;
    if (test) {
        result = selfTest(".");
    } else {
        WNDCLASSW type{};
        type.hInstance = instance;
        type.lpfnWndProc = procedure;
        type.lpszClassName = L"CrabitLiveFaces";
        type.hCursor = LoadCursor(nullptr, IDC_ARROW);
        type.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&type);
        HWND window = CreateWindowW(type.lpszClassName, L"Crabit · 表盘与8px设置 · 可操作离线预览",
                                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 850, 500,
                                    nullptr, nullptr, instance, nullptr);
        ShowWindow(window, show);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        result = int(message.wParam);
    }
    AppManager::shutdown();
    return result;
}
