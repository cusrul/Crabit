#include "src/product/src/NanoUI_Preflight.h"
#include <NanoUILegacy.h>
#include "src/product/app/app_wifi_sniff.h"

// Independent BW16 only. Actual App/SDK/framework; RAM screen, no OLED pins.
volatile bool is_display_active = true;
volatile bool ble_is_authenticated = false;
volatile bool ble_was_connected = false;
WifiHackContext* g_hackCtx = nullptr;
namespace NetManager {
volatile uint8_t active_conn_id = 255;
RadioSvc::Result safe_ble_notify(String, uint8_t) {
    return RadioSvc::REJECTED;
}
}  // namespace NetManager

namespace {
enum class Page { Unknown, Mode, Diagnostic, List, Forbidden };
Page visiblePage = Page::Unknown;
uint32_t displayedFrames = 0;
int displayedCount = -1;

// Run real Canvas text/font/layout calculations, with pixels in RAM only.
class RamCanvas : public Canvas {
    uint8_t pixels[1024] = {};

   public:
    void clear() override { memset(pixels, 0, sizeof(pixels)); }
    void display() override { ++displayedFrames; }
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64)
            return;
        uint8_t& byte = pixels[x + (y / 8) * 128];
        const uint8_t mask = 1u << (y % 8);
        if (color == C_INVERT)
            byte ^= mask;
        else if (color == C_WHITE)
            byte |= mask;
        else
            byte &= ~mask;
    }
    void drawText(int16_t x, int16_t y, const char* text, uint8_t size = 12, bool inverted = false,
                  uint8_t font = 0) override {
        if (text && y <= 2) {
            if (strcmp(text, "选择扫描方式") == 0)
                visiblePage = Page::Mode;
            else if (strcmp(text, "接收诊断") == 0)
                visiblePage = Page::Diagnostic;
            else if (strncmp(text, "信道:", strlen("信道:")) == 0) {
                visiblePage = Page::List;
                const char* count = strstr(text, "网络:");
                if (count)
                    displayedCount = atoi(count + strlen("网络:"));
            } else if (strcmp(text, "选择任务") == 0)
                visiblePage = Page::Forbidden;
        }
        Canvas::drawText(x, y, text, size, inverted, font);
    }
};

class BenchInput : public InputDriver {
    enum class Step {
        Select,
        Start,
        WaitPage,
        Tune,
        Capture,
        WaitList,
        Observe,
        BackMode,
        Exit,
        Done
    };
    Step step = Step::Select;
    bool native = false;
    uint32_t started = 0, changed = 0, lastEvent = 0, lastReport = 0;
    unsigned int tuneSteps = 0;
    UIEvent send(EventType event, Step next) {
        step = next;
        if (next == Step::Exit)
            finished = true;
        changed = lastEvent = millis();
        Serial.print("INPUT event=");
        Serial.print(static_cast<int>(event));
        Serial.print(" page=");
        Serial.println(static_cast<int>(visiblePage));
        return {event, 0};
    }
    UIEvent abort(const char* reason) {
        Serial.print("CASE_ABORT reason=");
        Serial.println(reason);
        is_display_active = false;
        step = Step::Done;
        return {EVT_NONE, 0};
    }

   public:
    bool finished = false;
    void init() override {}
    void begin(bool nativeCase) {
        native = nativeCase;
        step = Step::Select;
        started = changed = lastEvent = lastReport = millis();
        tuneSteps = 0;
        finished = false;
        visiblePage = Page::Unknown;
        displayedCount = -1;
        displayedFrames = 0;
        is_display_active = true;
    }
    UIEvent poll() override {
        const uint32_t now = millis();
        if (step == Step::Done)
            return {EVT_NONE, 0};
        if (visiblePage == Page::Forbidden)
            return abort("unexpected_task_menu");
        if (now - started > 70000)
            return abort("case_budget");
        if (now - lastReport >= 1000) {
            lastReport = now;
            const auto rx = wifiSniffRxSnapshot();
            int actual = 0;
            const int get = wifi_get_channel(&actual);
            Serial.print("APP_TICK ms=");
            Serial.print(now - started);
            Serial.print(" page=");
            Serial.print(static_cast<int>(visiblePage));
            Serial.print(" step=");
            Serial.print(static_cast<int>(step));
            Serial.print(" ch=");
            Serial.print(actual);
            Serial.print(" get=");
            Serial.print(get);
            Serial.print(" mode=");
            Serial.print(g_hackCtx ? static_cast<int>(g_hackCtx->currentMode) : -1);
            Serial.print(" cb=");
            Serial.print(rx.callbacks);
            Serial.print(" beacon=");
            Serial.print(rx.beacons);
            Serial.print(" added=");
            Serial.print(rx.added);
            Serial.print(" shown=");
            Serial.print(displayedCount);
            Serial.print(" draws=");
            Serial.println(displayedFrames);
        }
        if (now - lastEvent < 150)
            return {EVT_NONE, 0};
        switch (step) {
            case Step::Select:
                if (visiblePage == Page::Mode) {
                    if (native)
                        return send(EVT_OK, Step::WaitPage);
                    return send(EVT_UP, Step::Start);
                }
                break;
            case Step::Start:
                if (visiblePage == Page::Mode)
                    return send(EVT_OK, Step::WaitPage);
                break;
            case Step::WaitPage:
                if (native && visiblePage == Page::Diagnostic)
                    return abort("native_capture_setup_failed_diagnostic_fallback");
                if (native && visiblePage == Page::List) {
                    step = Step::Observe;
                    changed = now;
                } else if (visiblePage == Page::Diagnostic) {
                    step = Step::Tune;
                    changed = now;
                } else if (now - changed > 20000)
                    return abort("entry_page_timeout");
                break;
            case Step::Tune: {
                if (visiblePage != Page::Diagnostic)
                    return abort("lost_diagnostic_page");
                int actual = 0;
                if (wifi_get_channel(&actual) == 0 && actual == 40) {
                    step = Step::Capture;
                    changed = now;
                    Serial.println("FIXED40_BEGIN");
                } else if (++tuneSteps <= static_cast<unsigned int>(NUM_DEEP_CHANNELS)) {
                    return send(EVT_DOWN, Step::Tune);
                } else
                    return abort("channel40_not_reached");
                break;
            }
            case Step::Capture:
                if (now - changed >= 8000 && visiblePage == Page::Diagnostic)
                    return send(EVT_OK, Step::WaitList);
                break;
            case Step::WaitList:
                if (visiblePage == Page::List) {
                    step = Step::Observe;
                    changed = now;
                } else if (now - changed > 2000)
                    return abort("list_entry_timeout");
                break;
            case Step::Observe:
                // Crucial: never emit OK in the list; index0 opens task selection.
                if (visiblePage != Page::List)
                    return abort("lost_list_page");
                if (now - changed >= 25000)
                    return send(EVT_BACK, Step::BackMode);
                break;
            case Step::BackMode:
                if (visiblePage == Page::Mode)
                    return send(EVT_BACK, Step::Exit);
                if (now - changed > 3000)
                    return abort("back_mode_timeout");
                break;
            case Step::Exit:
            case Step::Done:
                break;
        }
        if (step == Step::Exit)
            finished = true;
        return {EVT_NONE, 0};
    }
};

class BenchRoot : public Application {
   public:
    const char* getAppName() override { return "bench-root"; }
};
RamCanvas screen;
BenchInput input;
unsigned int caseNumber = 0;
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(1200);
    Serial.println("CRABIT_WIFI_APP_BENCH_V1 actual_app ram_canvas no_oled no_ble");
    Serial.println("pages unknown0 mode1 diag2 list3 forbidden4; cases passive/native/passive");
    HAL_Time::init([]() -> uint32_t { return millis(); }, [](uint32_t ms) { delay(ms); });
    AppManager::init(&screen, &input);
    AppManager::launch(new BenchRoot());
}

void loop() {
    if (caseNumber >= 3) {
        delay(1000);
        return;
    }
    const bool native = ++caseNumber == 2;
    Serial.print("CASE_BEGIN id=");
    Serial.print(caseNumber);
    Serial.print(" native=");
    Serial.println(native);
    input.begin(native);
    AppManager::launch(new AppWifiSniffer());
    // runExclusive has returned after its own SDK teardown calls (not a drain proof).
    Serial.print("CASE_RETURN id=");
    Serial.print(caseNumber);
    Serial.print(" scripted_exit=");
    Serial.print(input.finished);
    Serial.print(" final_count=");
    Serial.println(g_hackCtx ? g_hackCtx->scanResults.size() : 0);
    if (!input.finished) {
        caseNumber = 3;
        Serial.println("BENCH_HALTED");
    } else if (caseNumber == 3)
        Serial.println("BENCH_DONE_NOT_WATCH_ACCEPTANCE");
    delay(2000);
}
