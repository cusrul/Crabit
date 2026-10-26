#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <string>
#include <vector>
#include <deque>

#include "ameba_session.h"
#include "NanoUI/NanoUI.h"
#include "event_bus.h"
#include "companion_ble.h"
#include "companion_trace.h"
#include "app_coproc.h"

std::atomic<Crabit::AmebaPhase> Crabit::ameba_phase(Crabit::AmebaPhase::Inactive);
NanoUI::Sensor* NanoUI::fake_pat_sensor = nullptr;

static uint32_t fake_tick;
static bool observed_tick_wrap;
static std::vector<std::string> trace;
struct Diagnostic { Crabit::CompanionTrace::Event event; uint32_t a, b; };
static std::vector<Diagnostic> diagnostics;
void Crabit::CompanionTrace::record(Event event, uint16_t, uint32_t a, uint32_t b) {
    diagnostics.push_back({event, a, b});
}
static uint32_t exits;
bool fake_uart_rx_ready = true;
uint32_t fake_uart_rx_calls, fake_uart_rx_boot_tick, fake_uart_rx_call_tick;
bool fake_pb7_uart_af, fake_uart_receive_to_idle;
bool fake_mode_ble_adopt = true;
uint32_t fake_mode_ble_adopt_calls, fake_mode_ble_finish_calls;
std::string fake_mode_ble_finish_result;
static bool background_ble_active = false, background_ble_transitioning = false;
static bool background_ble_stop_result = true;
static unsigned background_ble_stop_calls = 0, background_ble_resume_calls = 0;
bool Crabit::CompanionBle::active() { return background_ble_active; }
bool Crabit::CompanionBle::transitioning() { return background_ble_transitioning; }
bool Crabit::CompanionBle::stop() {
    ++background_ble_stop_calls;
    trace.push_back("backgroundBleStop");
    return background_ble_stop_result;
}
void Crabit::CompanionBle::resumeAfterUi() {
    ++background_ble_resume_calls;
    trace.push_back("backgroundBleResume");
}
bool fake_try_publish_result = true;
uint16_t fake_try_publish_session = 0;
static NanoUI::SensorDataOptical pat_data;
class FakeOpticalSensor : public NanoUI::Sensor {
public:
    const NanoUI::SensorData* getData() const override { return &pat_data; }
};
static FakeOpticalSensor pat_sensor;
uint32_t gpio_a7 = 1, gpio_a6 = 1;
FakeSPI hspi3;
volatile bool g_coproc_active = false;
FakeGlobalStore GlobalStore = {};
WindowManager* WindowManager::instance = nullptr;
void recordHardware(const char* name) { trace.push_back(name); }
void SysAudio_play(int, int) { trace.push_back("SysAudio_play"); }
void Application::requestExit() { ++exits; trace.push_back("requestExit"); }
extern "C" bool STM32_PrepareAmebaUartRx(void) {
    ++fake_uart_rx_calls;
    fake_uart_rx_call_tick = HAL_GetTick();
    trace.push_back("STM32_PrepareAmebaUartRx");
    if (fake_uart_rx_ready) {
        fake_pb7_uart_af = true;
        fake_uart_receive_to_idle = true;
    }
    return fake_uart_rx_ready;
}

uint32_t HAL_GetTick() { return fake_tick; }
void vTaskDelay(uint32_t ms) {
    if (ms > UINT32_MAX - fake_tick) observed_tick_wrap = true;
    fake_tick += ms;
}
void EventBus::publish(const char* topic, const char* value) {
    trace.push_back(std::string("publish:") + topic + ":" + value);
}
bool EventBus::tryPublish(const char* topic, const char* value, uint16_t session) {
    trace.push_back(std::string("tryPublish:") + topic + ":" + value);
    fake_try_publish_session = session;
    return fake_try_publish_result;
}
namespace Crabit { namespace CompanionBle { uint16_t sessionId(); } }
uint16_t Crabit::CompanionBle::sessionId() { return 0x4a31; }
bool Crabit::CompanionBle::adoptModeBle() {
    if (strcmp(GlobalStore.ble_status, "ADVERTISING") != 0) return false;
    ++fake_mode_ble_adopt_calls;
    trace.push_back("adoptModeBle");
    return fake_mode_ble_adopt;
}
void Crabit::CompanionBle::finishModeBleStart(const char* result) {
    ++fake_mode_ble_finish_calls;
    fake_mode_ble_finish_result = result ? result : "";
    trace.push_back(std::string("finishModeBleStart:") + fake_mode_ble_finish_result);
}
void unused_request_exit_symbol() {}

#include "app_cyber_link.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static void reset() {
    background_ble_active = background_ble_transitioning = false;
    background_ble_stop_result = true;
    background_ble_stop_calls = background_ble_resume_calls = 0;
    Popup::messages().clear();
    fake_tick = 0;
    observed_tick_wrap = false;
    trace.clear();
    diagnostics.clear();
    exits = 0;
    fake_uart_rx_ready = true;
    fake_mode_ble_adopt = true;
    fake_mode_ble_adopt_calls = fake_mode_ble_finish_calls = 0;
    fake_mode_ble_finish_result.clear();
    fake_try_publish_result = true;
    fake_try_publish_session = 0;
    fake_uart_rx_calls = fake_uart_rx_boot_tick = fake_uart_rx_call_tick = 0;
    fake_pb7_uart_af = true;
    fake_uart_receive_to_idle = true;
    NanoUI::fake_pat_sensor = nullptr;
    pat_data = NanoUI::SensorDataOptical{};
    g_coproc_active = false;
    GlobalStore.ble_status[0] = '\0';
    gpio_a7 = GPIO_PIN_SET;
    gpio_a6 = GPIO_PIN_SET;
    Crabit::setAmebaPhase(Crabit::AmebaPhase::Inactive);
}
static void update(CyberLinkApp& app, uint32_t t) {
    if (t < fake_tick) observed_tick_wrap = true;
    fake_tick = t;
    app.onUpdate(t);
}
static bool has(const char* item) {
    for (const auto& entry : trace) if (entry == item) return true;
    return false;
}
static int countPrefix(const char* prefix) {
    int result = 0;
    for (const auto& entry : trace) if (entry.compare(0, strlen(prefix), prefix) == 0) ++result;
    return result;
}
static std::string nextSensorMessage(CyberLinkApp& app, int old_count) {
    for (int step = 0; step < 25 && countPrefix("publish:sns/all:") == old_count; ++step)
        update(app, fake_tick + 1);
    if (countPrefix("publish:sns/all:") == old_count) return std::string();
    return trace.back();
}
static void enterRunning(CyberLinkApp& app, Canvas& canvas, InputDriver& input,
                         uint32_t initial_tick = 0) {
    fake_tick = initial_tick;
    app.onMount(&canvas, &input);
    CHECK(app.getMode() == APP_MODE_FRAME);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);
    update(app, fake_tick); // boot, including bounded legacy helper delays
    CHECK(has("BW16_NormalBoot"));
    CHECK(!g_coproc_active);
    update(app, fake_tick + 1200);
    CHECK(!has("tryPublish:sys/mode:UI"));
    update(app, fake_tick + 1);
    update(app, fake_tick); // HANDOVER frame
    CHECK(!g_coproc_active);
    update(app, fake_tick + 149);
    CHECK(!g_coproc_active);
    update(app, fake_tick + 1);
    CHECK(g_coproc_active);
    CHECK(has("tryPublish:sys/mode:UI"));
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);
    update(app, fake_tick + 149);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);
    update(app, fake_tick + 1);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Running);
}

int main() {
    WindowManager wm;
    WindowManager::instance = &wm;
    reset();
    Canvas canvas;
    FakeInput input;

    // Background BLE adopts only after real ADVERTISING; successful adoption
    // transfers ownership without powering off the radio or touching SPI.
    CyberLinkApp background(CyberLinkApp::Mode::BleBackground);
    background.onMount(&canvas, &input);
    update(background, fake_tick);
    update(background, fake_tick + 1200);
    update(background, fake_tick + 1);
    update(background, fake_tick);
    update(background, fake_tick + 150);
    CHECK(has("tryPublish:sys/mode:BLE"));
    CHECK(fake_try_publish_session == 0x4a31); // Request/session ID rides the existing mode command.
    CHECK(!has("publish:sys/mode:BLE") && !has("tryPublish:sys/mode:UI"));
    update(background, fake_tick + 150);
    CHECK(Popup::saw("蓝牙正在启动") && !canvas.saw("等待蓝牙就绪"));
    CHECK(canvas.texts.empty() && wm.frames > 0); // Existing page + toast, no raw diagnostic screen.
    strcpy(GlobalStore.ble_status, "QUEUED");
    int bg_sensor_count = countPrefix("publish:sns/all:");
    update(background, fake_tick + 1);
    CHECK(countPrefix("publish:sns/all:") > bg_sensor_count);
    CHECK(fake_mode_ble_adopt_calls == 0 && exits == 0 && !has("BW16_ForceOff"));
    strcpy(GlobalStore.ble_status, "ADVERTISING");
    update(background, fake_tick + 20);
    CHECK(fake_mode_ble_adopt_calls == 1 && fake_mode_ble_finish_calls == 0);
    CHECK(exits == 1 && has("requestExit") && !has("BW16_ForceOff"));
    CHECK(!has("Safe_SPI_Release") && !has("Safe_SPI_Claim") && !g_coproc_active);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Inactive);

    // Queue rejection is fail-closed: no foreground fallback, then off and report.
    reset(); canvas = Canvas(); input = FakeInput{};
    fake_try_publish_result = false;
    CyberLinkApp bg_queue_fail(CyberLinkApp::Mode::BleBackground);
    bg_queue_fail.onMount(&canvas, &input);
    update(bg_queue_fail, fake_tick);
    update(bg_queue_fail, fake_tick + 1200);
    update(bg_queue_fail, fake_tick + 1);
    update(bg_queue_fail, fake_tick);
    update(bg_queue_fail, fake_tick + 150);
    CHECK(Popup::saw("串口发送失败") && canvas.texts.empty());
    CHECK(!has("publish:sys/mode:BLE") && countPrefix("tryPublish:sys/mode:BLE") == 1 && fake_mode_ble_adopt_calls == 0);
    update(bg_queue_fail, fake_tick + 1);
    CHECK(has("BW16_ForceOff"));
    update(bg_queue_fail, fake_tick + 250);
    CHECK(fake_mode_ble_finish_calls == 1 && fake_mode_ble_finish_result == "UART SEND FAILED");

    // No advertisement by 5s expires, powers off first, then reports timeout.
    reset(); canvas = Canvas(); input = FakeInput{};
    CyberLinkApp bg_timeout(CyberLinkApp::Mode::BleBackground);
    bg_timeout.onMount(&canvas, &input);
    update(bg_timeout, fake_tick);
    update(bg_timeout, fake_tick + 1200);
    update(bg_timeout, fake_tick + 1);
    update(bg_timeout, fake_tick);
    update(bg_timeout, fake_tick + 150);
    update(bg_timeout, fake_tick + 150);
    update(bg_timeout, fake_tick + 5000);
    CHECK(Popup::saw("蓝牙启动超时") && fake_mode_ble_finish_calls == 0);
    update(bg_timeout, fake_tick + 1);
    CHECK(has("BW16_ForceOff"));
    update(bg_timeout, fake_tick + 250);
    CHECK(fake_mode_ble_finish_calls == 1 && fake_mode_ble_finish_result == "BLE TIMEOUT");

    // BLE FAILED and a rejected ownership reservation both eventually fail
    // closed through the same off-wait/report sequence.
    reset(); canvas = Canvas(); input = FakeInput{};
    CyberLinkApp bg_start_fail(CyberLinkApp::Mode::BleBackground);
    bg_start_fail.onMount(&canvas, &input);
    update(bg_start_fail, fake_tick); update(bg_start_fail, fake_tick + 1200);
    update(bg_start_fail, fake_tick + 1); update(bg_start_fail, fake_tick);
    update(bg_start_fail, fake_tick + 150); update(bg_start_fail, fake_tick + 150);
    strcpy(GlobalStore.ble_status, "FAILED");
    update(bg_start_fail, fake_tick + 1);
    CHECK(Popup::saw("蓝牙启动失败"));
    update(bg_start_fail, fake_tick + 1); update(bg_start_fail, fake_tick + 250);
    CHECK(fake_mode_ble_finish_calls == 1 && fake_mode_ble_finish_result == "BLE START FAILED");

    reset(); canvas = Canvas(); input = FakeInput{};
    fake_mode_ble_adopt = false;
    CyberLinkApp bg_adopt_reject(CyberLinkApp::Mode::BleBackground);
    bg_adopt_reject.onMount(&canvas, &input);
    update(bg_adopt_reject, fake_tick); update(bg_adopt_reject, fake_tick + 1200);
    update(bg_adopt_reject, fake_tick + 1); update(bg_adopt_reject, fake_tick);
    update(bg_adopt_reject, fake_tick + 150); update(bg_adopt_reject, fake_tick + 150);
    strcpy(GlobalStore.ble_status, "ADVERTISING");
    update(bg_adopt_reject, fake_tick + 1);
    CHECK(fake_mode_ble_adopt_calls == 1 && exits == 0 && !has("BW16_ForceOff"));
    update(bg_adopt_reject, fake_tick + 5000);
    update(bg_adopt_reject, fake_tick + 1); update(bg_adopt_reject, fake_tick + 250);
    CHECK(fake_mode_ble_finish_calls == 1 && fake_mode_ble_finish_result == "BLE TIMEOUT");

    // UART RX setup failure also shuts down and reports only after the 250ms wait.
    reset(); canvas = Canvas(); input = FakeInput{};
    fake_uart_rx_ready = false;
    CyberLinkApp bg_rx_fail(CyberLinkApp::Mode::BleBackground);
    bg_rx_fail.onMount(&canvas, &input);
    update(bg_rx_fail, fake_tick); update(bg_rx_fail, fake_tick + 1200);
    update(bg_rx_fail, fake_tick + 1);
    CHECK(Popup::saw("串口接收失败") && !has("tryPublish:sys/mode:BLE"));
    update(bg_rx_fail, fake_tick + 1); update(bg_rx_fail, fake_tick + 250);
    CHECK(fake_mode_ble_finish_calls == 1 && fake_mode_ble_finish_result == "UART RX FAILED");

    // Physical escape during startup follows the same off and finish contract.
    reset(); canvas = Canvas(); input = FakeInput{};
    CyberLinkApp bg_escape(CyberLinkApp::Mode::BleBackground);
    bg_escape.onMount(&canvas, &input);
    update(bg_escape, fake_tick); update(bg_escape, fake_tick + 1200);
    update(bg_escape, fake_tick + 1); update(bg_escape, fake_tick);
    update(bg_escape, fake_tick + 150); update(bg_escape, fake_tick + 150);
    gpio_a7 = gpio_a6 = GPIO_PIN_RESET;
    update(bg_escape, fake_tick + 1); update(bg_escape, fake_tick + 2001);
    update(bg_escape, fake_tick + 1);
    CHECK(has("BW16_ForceOff"));
    update(bg_escape, fake_tick + 250);
    CHECK(fake_mode_ble_finish_calls == 1 && fake_mode_ble_finish_result == "OFF");

    reset(); canvas = Canvas(); input = FakeInput{};
    // The BLE entry reuses the FRAME lifecycle but keeps STM32 OLED/SPI owned.
    CyberLinkApp ble(CyberLinkApp::Mode::Ble);
    ble.onMount(&canvas, &input);
    CHECK(ble.getMode() == APP_MODE_FRAME);
    CHECK(strcmp(ble.getAppName(), "AMEBA_BLE_MODE") == 0);
    strcpy(GlobalStore.ble_status, "STALE_STATUS");
    // onMount clears stale status; set it first and remount to exercise that contract.
    ble.onMount(&canvas, &input);
    CHECK(GlobalStore.ble_status[0] == '\0');
    update(ble, fake_tick);
    CHECK(has("BW16_NormalBoot"));
    CHECK(!g_coproc_active && !has("Safe_SPI_Release"));
    update(ble, fake_tick + 1200);
    CHECK(!has("publish:sys/mode:BLE") && !has("tryPublish:sys/mode:BLE"));
    update(ble, fake_tick + 1); // RX prepare, then BLE handover screen
    update(ble, fake_tick); // paint the BLE handover screen
    CHECK(fake_uart_rx_calls == 1 && canvas.saw("正在启动蓝牙"));
    update(ble, fake_tick); // begin common 150ms handover wait
    update(ble, fake_tick + 149);
    CHECK(!has("publish:sys/mode:BLE") && !has("tryPublish:sys/mode:BLE"));
    update(ble, fake_tick + 1);
    CHECK(has("tryPublish:sys/mode:BLE"));
    CHECK(!has("tryPublish:sys/mode:UI") && !has("publish:sys/comm"));
    CHECK(!has("Safe_SPI_Release") && !g_coproc_active);
    update(ble, fake_tick + 149);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);
    update(ble, fake_tick + 1);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Running);
    CHECK(canvas.saw("等待蓝牙就绪"));
    update(ble, fake_tick + 1);
    CHECK(countPrefix("publish:sns/all:") > 0);
    input.events.push_back(UIEvent{EVT_UP, 0});
    update(ble, fake_tick + 1);
    update(ble, fake_tick + 19);
    CHECK(trace.back().find("publish:sns/all:") == 0);
    CHECK(trace.back().find("|0|1") != std::string::npos);
    strcpy(GlobalStore.ble_status, "PAIR_CONFIRMED");
    update(ble, fake_tick + 1);
    CHECK(canvas.saw("连接已确认"));
    CHECK(input.polls > 0); // BLE status rendering does not bypass FRAME input.

    gpio_a7 = gpio_a6 = GPIO_PIN_RESET;
    update(ble, fake_tick + 1);
    update(ble, fake_tick + 2000);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Running);
    update(ble, fake_tick + 1);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);
    update(ble, fake_tick + 1); // ForceOff
    CHECK(has("BW16_ForceOff"));
    update(ble, fake_tick + 249);
    CHECK(!canvas.saw("已返回手表"));
    update(ble, fake_tick + 1); // 250ms off wait, direct recovered wait
    CHECK(canvas.saw("已返回手表"));
    CHECK(!has("Safe_SPI_Claim") && !has("HAL_SPI_DeInit") &&
          !has("HAL_SPI_Init") && !has("OLED_Init") && !has("OLED_SetSleep:false"));
    CHECK(!g_coproc_active);
    update(ble, fake_tick + 799);
    CHECK(exits == 0);
    update(ble, fake_tick + 1);
    CHECK(exits == 1 && has("requestExit"));
    ble.onUnmount();
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Inactive);

    // A failed BLE RX preparation remains terminal and still allows the physical escape.
    canvas = Canvas();
    reset();
    fake_uart_rx_ready = false;
    CyberLinkApp ble_rx_fail(CyberLinkApp::Mode::Ble);
    FakeInput ble_rx_input;
    ble_rx_fail.onMount(&canvas, &ble_rx_input);
    update(ble_rx_fail, fake_tick);
    update(ble_rx_fail, fake_tick + 1200);
    update(ble_rx_fail, fake_tick + 1);
    CHECK(canvas.saw("串口接收失败"));
    CHECK(!has("publish:sys/mode:BLE") && !has("tryPublish:sys/mode:BLE") && !has("Safe_SPI_Release"));
    for (int i = 0; i < 5; ++i) update(ble_rx_fail, fake_tick + 1000);
    CHECK(fake_uart_rx_calls == 1);
    gpio_a7 = gpio_a6 = GPIO_PIN_RESET;
    update(ble_rx_fail, fake_tick + 1);
    update(ble_rx_fail, fake_tick + 2001);
    update(ble_rx_fail, fake_tick + 1);
    CHECK(has("BW16_ForceOff"));
    CHECK(!has("Safe_SPI_Claim") && !has("HAL_SPI_DeInit") && !has("OLED_Init"));

    canvas = Canvas();
    reset();
    FakeInput ui_input;
    CyberLinkApp app;
    enterRunning(app, canvas, ui_input);

    CHECK(canvas.saw("正在进入 BW16"));
    CHECK(trace.size() >= 2 && trace[trace.size()-2] == "Safe_SPI_Release" &&
          trace.back() == "tryPublish:sys/mode:UI");
    CHECK(canvas.displayCalls == 4);
    CHECK(ui_input.polls == 0);

    // Execute 1000 production frame updates to establish bounded iteration.
    for (uint32_t n = 0; n < 1000; ++n) {
        update(app, fake_tick + 1);
    }
    CHECK(ui_input.polls == 1000); // exactly one drain pass per frame, then EVT_NONE
    CHECK(countPrefix("publish:sns/all:") >= 49 && countPrefix("publish:sns/all:") <= 51);
    CHECK(canvas.displayCalls == 4); // boot, two await updates, handover; no OLED RUN repaint

    // A published sensor snapshot is consumed once per valid timestamp.
    pat_data.timestamp = 0; // Valid tick-zero sample is distinct from "unseen".
    pat_data.is_valid = true;
    pat_data.dx = 4;
    pat_data.dy = -3;
    NanoUI::fake_pat_sensor = &pat_sensor;
    int sensor_count = countPrefix("publish:sns/all:");
    CHECK(nextSensorMessage(app, sensor_count) == "publish:sns/all:0,0,0|4,-3,1|0|0");
    sensor_count = countPrefix("publish:sns/all:");
    CHECK(nextSensorMessage(app, sensor_count) == "publish:sns/all:0,0,0|0,0,1|0|0");

    // Invalid data at the old timestamp is not replayed; a fresh timestamp is.
    pat_data.is_valid = false;
    pat_data.dx = 8;
    pat_data.dy = 6;
    sensor_count = countPrefix("publish:sns/all:");
    CHECK(nextSensorMessage(app, sensor_count) == "publish:sns/all:0,0,0|0,0,1|0|0");
    pat_data.timestamp = 1;
    pat_data.is_valid = true;
    pat_data.dx = -2;
    pat_data.dy = 5;
    sensor_count = countPrefix("publish:sns/all:");
    CHECK(nextSensorMessage(app, sensor_count) == "publish:sns/all:0,0,0|-2,5,1|0|0");
    NanoUI::fake_pat_sensor = nullptr;

    const int beforeEventSend = countPrefix("publish:sns/all:");
    const int pollsBeforeEvent = ui_input.polls;
    ui_input.events.push_back(UIEvent{EVT_UP, 0});
    update(app, fake_tick + 1);
    CHECK(ui_input.polls == pollsBeforeEvent + 2);
    int previous = countPrefix("publish:sns/all:");
    if (previous == beforeEventSend)
        while (countPrefix("publish:sns/all:") == previous) update(app, fake_tick + 1);
    CHECK(trace.back().find("publish:sns/all:0,0,0|0,0,1|0|1") == 0);
    previous = countPrefix("publish:sns/all:");
    update(app, fake_tick + 19);
    CHECK(countPrefix("publish:sns/all:") == previous);
    update(app, fake_tick + 1);
    CHECK(countPrefix("publish:sns/all:") == previous + 1);
    CHECK(canvas.displayCalls == 4);

    // Held physical UP+DOWN triggers only after strictly more than 2000ms.
    gpio_a7 = GPIO_PIN_RESET; gpio_a6 = GPIO_PIN_RESET;
    update(app, fake_tick + 1);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Running);
    update(app, fake_tick + 2000);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Running);
    update(app, fake_tick + 1);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);
    CHECK(!has("requestExit"));
    update(app, fake_tick + 1); // ForceOff call, including its existing 50ms delay
    CHECK(has("BW16_ForceOff"));
    update(app, fake_tick + 249);
    CHECK(!has("Safe_SPI_Claim"));
    update(app, fake_tick + 1); // exact 250ms off wait, advance to restore
    update(app, fake_tick); // claim/deinit and begin 5ms SPI settle
    CHECK(has("Safe_SPI_Claim"));
    CHECK(!has("HAL_SPI_Init"));
    update(app, fake_tick + 4);
    CHECK(!has("HAL_SPI_Init"));
    update(app, fake_tick + 1); // exact SPI wait
    CHECK(has("HAL_SPI_Init"));
    CHECK(has("OLED_Init"));
    CHECK(has("OLED_SetSleep:false"));
    update(app, fake_tick + 49);
    CHECK(g_coproc_active);
    CHECK(!canvas.saw("已返回手表"));
    update(app, fake_tick + 1); // exact 50ms display wake wait
    CHECK(!g_coproc_active);
    CHECK(canvas.saw("已返回手表"));
    update(app, fake_tick + 799);
    CHECK(exits == 0);
    update(app, fake_tick + 1); // exact 800ms recovered-screen wait
    CHECK(exits == 1 && has("requestExit"));
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);
    app.onUnmount();
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Inactive);
    CHECK(background_ble_stop_calls == 0 && background_ble_resume_calls == 0);

    // The physical UP+DOWN hold itself crosses tick wrap and remains strict >2s.
    reset();
    const uint32_t hold_startup = UINT32_MAX - 2500;
    CyberLinkApp hold_wrap;
    FakeInput hold_input;
    enterRunning(hold_wrap, canvas, hold_input, hold_startup);
    CHECK(!observed_tick_wrap); // startup ends before wrap
    gpio_a7 = GPIO_PIN_RESET;
    gpio_a6 = GPIO_PIN_RESET;
    update(hold_wrap, fake_tick + 1); // start held interval
    update(hold_wrap, fake_tick + 2000);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Running);
    CHECK(observed_tick_wrap);
    update(hold_wrap, fake_tick + 1);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);
    CHECK(!has("requestExit"));

    // An interrupted instance remains fail-closed when the manager removes it.
    reset();
    CyberLinkApp interrupted;
    interrupted.onMount(&canvas, &input);
    interrupted.onUnmount();
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);

    // Reentry is a new application instance, matching AppManager ownership.
    reset();
    CyberLinkApp reentered;
    FakeInput reentryInput;
    enterRunning(reentered, canvas, reentryInput);
    CHECK(countPrefix("publish:sns/all:") == 0);
    reentered.onUnmount();
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);

    // RX repair must run after the boot wait and before handover side effects.
    reset();
    CyberLinkApp uart_ok;
    FakeInput uart_input;
    uart_ok.onMount(&canvas, &uart_input);
    update(uart_ok, fake_tick);
    CHECK(fake_uart_rx_calls == 0);
    update(uart_ok, fake_tick + 1200);
    CHECK(fake_uart_rx_calls == 0);
    update(uart_ok, fake_tick + 1);
    CHECK(fake_uart_rx_calls == 1);
    CHECK(fake_pb7_uart_af && fake_uart_receive_to_idle);
    CHECK(fake_uart_rx_call_tick - fake_uart_rx_boot_tick >= 1201);
    CHECK(!has("Safe_SPI_Release") && !has("tryPublish:sys/mode:UI"));
    update(uart_ok, fake_tick); // render HANDING OVER and begin existing 150ms wait
    CHECK(canvas.saw("正在进入 BW16"));
    CHECK(!has("Safe_SPI_Release"));
    update(uart_ok, fake_tick + 149);
    CHECK(!has("Safe_SPI_Release"));
    update(uart_ok, fake_tick + 1);
    CHECK(has("Safe_SPI_Release") && has("tryPublish:sys/mode:UI"));

    // Failed RX repair is terminal: no handover or retries; physical escape remains.
    reset();
    fake_uart_rx_ready = false;
    CyberLinkApp uart_fail;
    FakeInput uart_fail_input;
    uart_fail.onMount(&canvas, &uart_fail_input);
    update(uart_fail, fake_tick);
    update(uart_fail, fake_tick + 1200);
    update(uart_fail, fake_tick + 1);
    CHECK(fake_uart_rx_calls == 1);
    CHECK(!fake_pb7_uart_af && !fake_uart_receive_to_idle);
    CHECK(canvas.saw("串口接收失败"));
    CHECK(!has("Safe_SPI_Release") && !has("tryPublish:sys/mode:UI"));
    for (int i = 0; i < 20; ++i) update(uart_fail, fake_tick + 1000);
    CHECK(fake_uart_rx_calls == 1 && uart_fail_input.polls == 0);
    CHECK(!has("Safe_SPI_Release") && !has("tryPublish:sys/mode:UI"));
    gpio_a7 = gpio_a6 = GPIO_PIN_RESET;
    update(uart_fail, fake_tick + 1);
    update(uart_fail, fake_tick + 2001);
    CHECK(!has("BW16_ForceOff"));
    update(uart_fail, fake_tick + 1); // existing separate PowerOff state step
    CHECK(has("BW16_ForceOff"));
    CHECK(fake_uart_rx_calls == 1);

    // Unsigned elapsed time works across HAL tick rollover.
    reset();
    const uint32_t near_wrap = UINT32_MAX - 800;
    CyberLinkApp wrap;
    FakeInput input3;
    enterRunning(wrap, canvas, input3, near_wrap);
    CHECK(observed_tick_wrap);
    CHECK(fake_tick < near_wrap);
    update(wrap, fake_tick + 20u);
    CHECK(countPrefix("publish:sns/all:") == 1);
    wrap.onUnmount();
    // WIFI uses the proven boot and RX repair without releasing STM32's OLED.
    reset();
    Crabit::NetworkWeather::activeFlag() = true;
    CyberLinkApp wifi(CyberLinkApp::Mode::Wifi);
    FakeInput wifi_input;
    wifi.onMount(&canvas, &wifi_input);
    update(wifi, fake_tick);
    update(wifi, fake_tick + 1201);
    update(wifi, fake_tick);
    update(wifi, fake_tick + 150);
    CHECK(has("BW16_NormalBoot") && fake_uart_rx_calls == 1);
    CHECK(has("tryPublish:sys/mode:WIFI") && fake_try_publish_session == 41);
    CHECK(!g_coproc_active && !has("Safe_SPI_Release"));
    update(wifi, fake_tick + 150);
    update(wifi, fake_tick + 1);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Running);
    CHECK(countPrefix("publish:sns/all:") == 0);
    Crabit::NetworkWeather::activeFlag() = false;
    update(wifi, fake_tick + 1);
    update(wifi, fake_tick + 1);
    CHECK(has("BW16_ForceOff"));
    update(wifi, fake_tick + 250);
    update(wifi, fake_tick + 800);
    wifi.onUnmount();
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Inactive);
    CHECK(!has("Safe_SPI_Claim") && !has("OLED_Init"));
    // Background BLE must yield before any BW16 boot or OLED handover. Resume
    // is queued only after the original complete UI exit/restore sequence.
    reset();
    canvas = Canvas();
    background_ble_active = true;
    CyberLinkApp automatic_ui;
    FakeInput automatic_input;
    automatic_ui.onMount(&canvas, &automatic_input);
    CHECK(canvas.saw("正在暂停蓝牙") && !has("BW16_NormalBoot"));
    update(automatic_ui, fake_tick);
    CHECK(background_ble_stop_calls == 1 && !has("BW16_NormalBoot"));
    update(automatic_ui, fake_tick + 100);
    CHECK(background_ble_stop_calls == 1 && !has("Safe_SPI_Release"));
    background_ble_active = false;
    background_ble_transitioning = true;
    update(automatic_ui, fake_tick + 100);
    CHECK(!has("BW16_NormalBoot"));
    background_ble_transitioning = false;
    update(automatic_ui, fake_tick + 50);
    CHECK(!has("BW16_NormalBoot")); // stop completion, not same-frame boot
    update(automatic_ui, fake_tick + 1);
    CHECK(has("BW16_NormalBoot"));
    update(automatic_ui, fake_tick + 1201);
    update(automatic_ui, fake_tick);
    update(automatic_ui, fake_tick + 150);
    update(automatic_ui, fake_tick + 150);
    CHECK(has("tryPublish:sys/mode:UI") && g_coproc_active);
    CHECK(background_ble_resume_calls == 0);
    gpio_a7 = gpio_a6 = GPIO_PIN_RESET;
    update(automatic_ui, fake_tick + 1);
    update(automatic_ui, fake_tick + 2001);
    update(automatic_ui, fake_tick + 1);
    update(automatic_ui, fake_tick + 250);
    update(automatic_ui, fake_tick + 1);
    update(automatic_ui, fake_tick + 5);
    update(automatic_ui, fake_tick + 50);
    CHECK(has("OLED_Init") && !g_coproc_active && background_ble_resume_calls == 0);
    update(automatic_ui, fake_tick + 800);
    CHECK(background_ble_resume_calls == 0); // must unmount before restoration
    automatic_ui.onUnmount();
    CHECK(background_ble_resume_calls == 1);
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Inactive);
    CHECK(trace.back() == "backgroundBleResume");

    // No blind takeover when STOP fails or never completes, including wrap.
    reset();
    background_ble_active = true;
    background_ble_stop_result = false;
    CyberLinkApp rejected_ui;
    rejected_ui.onMount(&canvas, &automatic_input);
    update(rejected_ui, fake_tick);
    rejected_ui.onUnmount();
    CHECK(Popup::saw("蓝牙关闭失败") && background_ble_stop_calls == 1);
    CHECK(!has("BW16_NormalBoot") && !has("Safe_SPI_Release") && background_ble_resume_calls == 0);
    reset();
    fake_tick = 0xfffffff0u;
    background_ble_active = background_ble_transitioning = true;
    CyberLinkApp timeout_ui;
    timeout_ui.onMount(&canvas, &automatic_input);
    update(timeout_ui, fake_tick + 6000u);
    timeout_ui.onUnmount();
    CHECK(Popup::saw("蓝牙关闭超时") && !has("BW16_NormalBoot"));
    CHECK(background_ble_stop_calls == 0 && background_ble_resume_calls == 0);
    reset();
    background_ble_active = true;
    CyberLinkApp interrupted_ui;
    interrupted_ui.onMount(&canvas, &automatic_input);
    update(interrupted_ui, fake_tick);
    interrupted_ui.onUnmount();
    CHECK(background_ble_resume_calls == 0 && !has("BW16_NormalBoot"));
    CHECK(Crabit::ameba_phase.load() == Crabit::AmebaPhase::Transition);

    // Diagnostics must reveal a dropped mode command without adding a retry,
    // ACK wait, rollback or different hardware sequence in this evidence build.
    reset();
    fake_try_publish_result = false;
    CyberLinkApp queue_full_ui;
    FakeInput queue_full_input;
    enterRunning(queue_full_ui, canvas, queue_full_input);
    CHECK(countPrefix("tryPublish:sys/mode:UI") == 1 && g_coproc_active);
    bool saw_failed_admission = false, saw_running_heartbeat = false;
    update(queue_full_ui, fake_tick + 1001);
    for (const auto& item : diagnostics) {
        if (item.event == Crabit::CompanionTrace::ModeQueued && item.a == 0 && item.b == 0)
            saw_failed_admission = true;
        if (item.event == Crabit::CompanionTrace::UiHeartbeat && item.a == 7 && item.b == 1)
            saw_running_heartbeat = true;
    }
    CHECK(saw_failed_admission && saw_running_heartbeat);
    CHECK(countPrefix("tryPublish:sys/mode:UI") == 1);
    queue_full_ui.onUnmount();
    CHECK(diagnostics.back().event == Crabit::CompanionTrace::UiStage &&
          (diagnostics.back().b & 8u) != 0);

    if (failures) return 1;
    puts("STM32 AMEBA frame host checks passed");
    return 0;
}
