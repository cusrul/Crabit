#define CRABIT_NANOUI_SIM_BUILD
#include "simulator.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <queue>

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/memory_pool.h"
#include "NanoUI/hal/hal_time.h"
#include "app_real/launcher.h"
#include "app_real/app_clock.h"

uint32_t SystemCoreClock = 80000000u;

namespace {

uint32_t injectedNow = 0;
uint32_t frameGeneration = 0;
bool hasActiveHost = false;
uint32_t hostLocalEpoch = 1792886400u;
uint32_t hostEpochTick = 0;

uint32_t readInjectedTime() { return injectedNow; }
void ignoreDelay(uint32_t) {}

class HeapAdapter final : public NanoUI::MemoryAdapter {
public:
    void* alloc(size_t size) override { return std::malloc(size); }
    void free(void* ptr) override { std::free(ptr); }
};

class FrameCanvas final : public Canvas {
public:
    void clear() override { pixels_.fill(0); }
    void display() override { ++frameGeneration; }
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    const uint8_t* data() const { return pixels_.data(); }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        const size_t index = static_cast<size_t>(y) * 16u + static_cast<unsigned>(x) / 8u;
        const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
        if (color == C_WHITE) pixels_[index] |= mask;
        else if (color == C_BLACK) pixels_[index] &= static_cast<uint8_t>(~mask);
        else if (color == C_INVERT) pixels_[index] ^= mask;
    }

private:
    std::array<uint8_t, 1024> pixels_{};
};

class QueuedInput final : public InputDriver {
public:
    void init() override { while (!events_.empty()) events_.pop(); }
    UIEvent poll() override {
        if (events_.empty()) return {EVT_NONE, 0};
        const UIEvent event = events_.front();
        events_.pop();
        return event;
    }
    void push(EventType type) { events_.push({type, 0}); }

private:
    std::queue<UIEvent> events_;
};

struct Host {
    HeapAdapter allocator;
    FrameCanvas canvas;
    QueuedInput input;
    WindowManager window{&canvas, &input};
    bool okHeld = false;
    bool longBackSent = false;
    uint32_t okDownAt = 0;
};

std::unique_ptr<Host> host;

void stepHost(uint32_t now_ms) {
    injectedNow = now_ms;
    if (!crabit_sim_clock_set()) {
        crabit_sim_clock_epoch() = hostLocalEpoch;
        crabit_sim_clock_tick() = hostEpochTick;
        crabit_sim_clock_set() = true;
    }
    crabit_sim_rtc_refresh();
    if (host->okHeld && !host->longBackSent &&
        static_cast<uint32_t>(now_ms - host->okDownAt) > 550u) {
        host->longBackSent = true;
        host->input.push(EVT_BACK);
    }
    AppManager::tick();
    host->window.update();
    host->window.drawFrame();
}

} // namespace

extern "C" int32_t crabit_sim_start(void) {
    try {
        if (host) return 0;
        injectedNow = 0;
        frameGeneration = 0;
        hostLocalEpoch = 1792886400u;
        hostEpochTick = 0;
        Crabit::NetworkWeather::clearPreviewWeather();
        crabit_sim_clock_set() = false;
        HAL_Time::init(readInjectedTime, ignoreDelay);
        host.reset(new Host());
        NanoUI::MemoryPool::instance().setAdapter(&host->allocator);
        host->input.init();
        AppManager::init(&host->canvas, &host->input);
        AppManager::launch(new LauncherApp());
        stepHost(injectedNow);
        hasActiveHost = true;
        return 0;
    } catch (...) {
        if (host) {
            try { AppManager::shutdown(); } catch (...) {}
            NanoUI::MemoryPool::instance().setAdapter(nullptr);
            host.reset();
        }
        hasActiveHost = false;
        Crabit::NetworkWeather::clearPreviewWeather();
        crabit_sim_clock_set() = false;
        return -1;
    }
}

extern "C" void crabit_sim_stop(void) {
    try {
        if (!host) return;
        AppManager::shutdown();
        NanoUI::MemoryPool::instance().setAdapter(nullptr);
        host.reset();
    } catch (...) {
        NanoUI::MemoryPool::instance().setAdapter(nullptr);
        host.reset();
    }
    hasActiveHost = false;
    Crabit::NetworkWeather::clearPreviewWeather();
    crabit_sim_clock_set() = false;
}

extern "C" void crabit_sim_step(uint32_t now_ms) {
    try { if (host) stepHost(now_ms); } catch (...) {}
}

extern "C" void crabit_sim_button(int32_t key, int32_t pressed, uint32_t now_ms) {
    try {
        if (!host) return;
        injectedNow = now_ms;
        if (key == 2) {
            if (pressed == 1) {
                if (!host->okHeld) {
                    host->okHeld = true;
                    host->longBackSent = false;
                    host->okDownAt = now_ms;
                }
            } else if (pressed == 0) {
                if (host->okHeld && !host->longBackSent) {
                    host->input.push(static_cast<uint32_t>(now_ms - host->okDownAt) > 550u
                        ? EVT_BACK : EVT_OK);
                }
                host->okHeld = false;
                host->longBackSent = false;
            } else if (pressed == -1) {
                host->okHeld = false;
                host->longBackSent = false;
            }
            return;
        }
        if (pressed == 1) {
            if (key == 0) host->input.push(EVT_UP);
            else if (key == 1) host->input.push(EVT_DOWN);
        }
    } catch (...) {}
}

extern "C" const uint8_t* crabit_sim_frame(void) {
    try { return host ? host->canvas.data() : nullptr; } catch (...) { return nullptr; }
}

extern "C" uint32_t crabit_sim_generation(void) {
    try { return hasActiveHost ? frameGeneration : 0; } catch (...) { return 0; }
}

namespace {
bool validWeatherDate(uint32_t date) {
    const unsigned year = date / 10000u;
    const unsigned month = date / 100u % 100u;
    const unsigned day = date % 100u;
    return year >= 2000 && year <= 2099 && day >= 1 &&
           day <= CrabitWeather::monthDays(year, month);
}
}
extern "C" int32_t crabit_sim_set_clock(uint32_t epoch) {
    if (!host || epoch < 946684800u || epoch > 4102444799u) return -1;
    hostLocalEpoch = epoch;
    hostEpochTick = injectedNow;
    crabit_sim_clock_epoch() = epoch;
    crabit_sim_clock_tick() = injectedNow;
    crabit_sim_clock_set() = true;
    crabit_sim_rtc_assign_fields(epoch);
    if (WindowManager::instance) WindowManager::instance->markDirty();
    return 0;
}
extern "C" int32_t crabit_sim_set_weather(uint32_t date, int32_t code, int32_t low,
                                             int32_t high, int32_t rain, uint32_t fetched) {
    if (!host) return -1;
    if (date == 0) {
        Crabit::NetworkWeather::clearPreviewWeather();
        if (WindowManager::instance) WindowManager::instance->markDirty();
        return 0;
    }
    if (!validWeatherDate(date) || code < 0 || code > 99 || low < -1000 || high > 1000 ||
        low > high || rain < 0 || rain > 100 || fetched < 946684800u ||
        fetched > 4102444799u) return -1;
    Crabit::NetworkWeather::setPreviewWeather(
        date, static_cast<uint16_t>(code), static_cast<int16_t>(low),
        static_cast<int16_t>(high), static_cast<uint8_t>(rain), fetched);
    if (WindowManager::instance) WindowManager::instance->markDirty();
    return 0;
}
extern "C" int32_t crabit_sim_show_clock(void) {
    if (!host) return -1;
    try {
        AppManager::shutdown();
        host->input.init();
        host->okHeld = false;
        host->longBackSent = false;
        host->okDownAt = 0;
        AppManager::init(&host->canvas, &host->input);
        AppManager::launch(new ClockApp());
        stepHost(injectedNow);
        return 0;
    } catch (...) { return -1; }
}
