"""Execute extracted RadioSvc checkpoint/start/service code against host doubles."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_companion_uart_probe import function


ROOT = Path(__file__).resolve().parents[2]


def production_fixture(radio, protocol):
    return (
        r"""
#include "crabit_wire.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using TickType_t = uint32_t;
using TaskHandle_t = void*;
using QueueHandle_t = void*;
constexpr int pdTRUE = 1;
#define pdMS_TO_TICKS(value) (value)

namespace RadioSvc {
enum State { IDLE, STARTING, ADVERTISING, CONNECTED, STOPPING, FAILED, TIMEOUT };
enum Result { QUEUED, ACQUIRED, BUSY, REJECTED, FAILED_RESULT, STOPPED_RESULT, TIMEOUT_RESULT };
}
enum Mode { MODE_WAITING, MODE_BLE };
Mode current_mode = MODE_BLE;
bool g_mode_ram_pairing = false, g_pair_diagnostic_active = false;
uint16_t g_mode_pair_session = 0;
std::string g_bound_pair_token;
bool isConfiguredAuthCodeValid() { return true; }
void processPairDiagnosticBegin() { g_pair_diagnostic_active = true; }
using T_APP_RESULT = int;
constexpr int APP_RESULT_SUCCESS = 0, APP_RESULT_ACCEPT = 1;
int radioGapCallback(uint8_t, void*) { return APP_RESULT_SUCCESS; }
void le_register_app_cb(T_APP_RESULT (*)(uint8_t, void*)) { }
bool enableConnectionIndicationCallback() { return true; }
"""
        + function(radio, "enum RadioTraceStage : uint8_t")
        + r""";
struct RadioTraceEvent;
"""
        + function(radio, "struct RadioTraceEvent")
        + r""";
int owner_token, foreign_token;
TaskHandle_t g_loop_owner = &owner_token, current_task = &owner_token;
bool g_owner_ready = true, g_companion_active = true;
QueueHandle_t g_trace_queue = reinterpret_cast<void*>(1);
volatile bool g_trace_overflow = false;
std::vector<RadioTraceEvent> async_events;
uint32_t now_ms = 0, g_start_deadline_ms = 5000, g_generation = 7;
volatile RadioSvc::State g_state = RadioSvc::STARTING;
unsigned write_calls = 0, flush_calls = 0;
bool short_write = false, mutate_generation_on_write = false;
bool mutate_state_on_write = false, expire_deadline_on_write = false;
unsigned short_write_call = 0;
std::vector<uint8_t> serial_bytes;
struct FakeSerial {
    size_t write(const uint8_t* data, size_t size) {
        ++write_calls;
        if (mutate_generation_on_write) ++g_generation;
        if (mutate_state_on_write) g_state = RadioSvc::FAILED;
        if (expire_deadline_on_write) now_ms = g_start_deadline_ms;
        size_t count = (short_write || write_calls == short_write_call) && size ? size - 1 : size;
        serial_bytes.insert(serial_bytes.end(), data, data + count);
        return count;
    }
    void flush() { ++flush_calls; }
} Serial;
uint32_t millis() { return now_ms; }
void delay(uint32_t amount) { now_ms += amount; }
TickType_t xTaskGetTickCount() { return now_ms; }
TaskHandle_t xTaskGetCurrentTaskHandle() { return current_task; }
bool generationIsCurrent(uint32_t generation) { return generation == g_generation; }
int xQueueSend(QueueHandle_t queue, const RadioTraceEvent* event, unsigned) {
    if (!queue || async_events.size() >= 4) return 0;
    async_events.push_back(*event);
    return pdTRUE;
}
bool enqueueRadioTrace(RadioTraceStage stage, uint16_t request_id) {
    if (!g_trace_queue) return false;
    const RadioTraceEvent event{request_id, stage};
    if (xQueueSend(g_trace_queue, &event, 0) == pdTRUE) return true;
    g_trace_overflow = true;
    return false;
}
namespace Protocol {
"""
        + function(protocol, "bool send_message_checked(")
        + r"""
bool checkedSend(const char* topic, const char* payload, uint16_t id) {
    return Protocol::send_message_checked(topic, payload, id);
}
} // namespace Protocol
"""
        + function(radio, "const char* radioTraceText(")
        + function(radio, "bool radioCheckpoint(")
        + r"""

namespace RadioSvc { }
struct RadioCommand { uint8_t kind; uint16_t request_id; uint32_t generation; uint32_t pair_seq; };
struct T_GAP_DEV_STATE { uint8_t gap_adv_state; };
constexpr int GAP_PARAM_DEV_STATE = 1, GAP_CAUSE_SUCCESS = 0, GAP_CAUSE_FAIL = 1;
constexpr uint8_t GAP_ADV_STATE_ADVERTISING = 2;
int gap_result = GAP_CAUSE_SUCCESS;
uint8_t observed_adv_state = 0;
unsigned gap_reads = 0, ble_connected_calls = 0, peripheral_begins = 0;
unsigned advertise_after_reads = 0;
unsigned adv_starts = 0, adv_stops = 0, fail_calls = 0, configure_calls = 0;
bool connected = false, g_peripheral_started = false, configure_ok = true;
struct FakeAdvert { void startAdv() { ++adv_starts; } void stopAdv() { ++adv_stops; } } advert;
struct FakeBLE {
    FakeAdvert* configAdvert() { return &advert; }
    void beginPeripheral() { ++peripheral_begins; }
    bool connected() { ++ble_connected_calls; return ::connected; }
} BLE;
int le_get_gap_param(int parameter, T_GAP_DEV_STATE* state) {
    assert(parameter == GAP_PARAM_DEV_STATE);
    ++gap_reads;
    if (advertise_after_reads && gap_reads >= advertise_after_reads)
        observed_adv_state = GAP_ADV_STATE_ADVERTISING;
    state->gap_adv_state = observed_adv_state; return gap_result;
}
bool configureBleOnce(uint32_t) { ++configure_calls; return configure_ok; }
void failGeneration(uint32_t generation, const char*) {
    ++fail_calls; if (generationIsCurrent(generation)) g_state = RadioSvc::FAILED;
}
void setState(RadioSvc::State state) { g_state = state; }
RadioSvc::Result g_last_result = RadioSvc::REJECTED;
void publishProtocolStatus(const char*, uint16_t) { }
void flushUartEvents();
namespace RadioSvc { void flushUartEvents() { ::flushUartEvents(); } }
"""
        + function(radio, "void processStart(")
        + r"""

unsigned service_order[3], service_calls;
void flushUartEvents() { service_order[service_calls++] = 1; }
void serviceOwner() { service_order[service_calls++] = 2; }
"""
        + function(radio, "void service()")
        + r"""

void reset() {
    g_loop_owner = current_task = &owner_token; g_owner_ready = true;
    g_companion_active = true; g_trace_queue = reinterpret_cast<void*>(1);
    g_trace_overflow = false; async_events.clear();
    current_mode = MODE_BLE; g_mode_ram_pairing = false;
    g_pair_diagnostic_active = false; g_mode_pair_session = 0;
    g_bound_pair_token.clear();
    now_ms = 0; g_start_deadline_ms = 5000; g_generation = 7;
    g_state = RadioSvc::STARTING; write_calls = flush_calls = 0;
    short_write = mutate_generation_on_write = false;
    mutate_state_on_write = expire_deadline_on_write = false; short_write_call = 0;
    serial_bytes.clear(); gap_result = GAP_CAUSE_SUCCESS; observed_adv_state = 0;
    gap_reads = ble_connected_calls = peripheral_begins = advertise_after_reads = 0;
    adv_starts = adv_stops = fail_calls = configure_calls = 0;
    connected = g_peripheral_started = false; configure_ok = true;
    service_calls = 0;
}

int main() {
    reset();
    assert(radioCheckpoint(TRACE_BLE_INIT_BEGIN, 41, 7));
#if CRABIT_BW16_DIAG_CHECKPOINTS
    assert(write_calls == 1 && async_events.empty());
    uint8_t payload[CrabitWire::PAYLOAD_SIZE]; size_t payload_size = 0;
    assert(CrabitWire::decode_frame(serial_bytes.data(), serial_bytes.size(), payload, sizeof(payload), &payload_size));
    CrabitWire::Message decoded{};
    assert(CrabitWire::decode_message(payload, payload_size, &decoded));
    assert(decoded.request_id == 41 && std::strcmp(decoded.topic, "diag/radio") == 0);
    assert(std::strcmp(decoded.value, "BLE_INIT_BEGIN") == 0 && flush_calls == 1);
#else
    assert(write_calls == 0 && async_events.size() == 1);
    assert(async_events[0].stage == TRACE_BLE_INIT_BEGIN && async_events[0].request_id == 41);
#endif

    reset(); current_task = &foreign_token;
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
    assert(async_events.empty() && write_calls == 0);
    reset(); g_loop_owner = nullptr;
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
    reset(); g_generation++;
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
    reset(); g_state = RadioSvc::FAILED;
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
    reset(); now_ms = 5000;
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
    reset(); g_start_deadline_ms = 0;
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
    reset(); now_ms = UINT32_MAX - 4; g_start_deadline_ms = now_ms + 20;
    assert(radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7)); // valid across tick wrap
    reset(); short_write = true;
#if CRABIT_BW16_DIAG_CHECKPOINTS
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
    assert(write_calls == 1 && flush_calls == 1);
#else
    assert(radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
#endif
    reset(); mutate_generation_on_write = true;
#if CRABIT_BW16_DIAG_CHECKPOINTS
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7)); // post-write generation guard
#else
    assert(radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
#endif
    reset(); mutate_state_on_write = true;
#if CRABIT_BW16_DIAG_CHECKPOINTS
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7)); // post-write state guard
#else
    assert(radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
#endif
    reset(); expire_deadline_on_write = true;
#if CRABIT_BW16_DIAG_CHECKPOINTS
    assert(!radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7)); // post-write original deadline guard
#else
    assert(radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
#endif
    reset(); g_companion_active = false;
    assert(radioCheckpoint(TRACE_PREFS_BEGIN, 1, 7));
    assert(async_events.size() == 1 && write_calls == 0);

    reset(); observed_adv_state = GAP_ADV_STATE_ADVERTISING;
    processStart(RadioCommand{0, 23, 7, 0});
    assert(g_state == RadioSvc::ADVERTISING && peripheral_begins == 1 && adv_starts == 0);
    assert(gap_reads == 1 && fail_calls == 0);
    reset(); advertise_after_reads = 3;
    processStart(RadioCommand{0, 23, 7, 0});
    assert(g_state == RadioSvc::ADVERTISING && gap_reads == 3 && now_ms == 20);
    reset(); gap_result = GAP_CAUSE_FAIL; observed_adv_state = GAP_ADV_STATE_ADVERTISING;
    processStart(RadioCommand{0, 23, 7, 0});
    assert(g_state == RadioSvc::FAILED && adv_stops == 1 && fail_calls == 1);
    reset(); connected = true;
    processStart(RadioCommand{0, 23, 7, 0});
    assert(g_state == RadioSvc::ADVERTISING && ble_connected_calls == 1);
    reset(); observed_adv_state = GAP_ADV_STATE_ADVERTISING; g_peripheral_started = true;
    processStart(RadioCommand{0, 23, 7, 0});
    assert(g_state == RadioSvc::ADVERTISING && peripheral_begins == 0 && adv_starts == 1);
    reset(); now_ms = UINT32_MAX - 15; g_start_deadline_ms = now_ms + 25;
    gap_result = GAP_CAUSE_FAIL;
    processStart(RadioCommand{0, 23, 7, 0});
    assert(g_state == RadioSvc::FAILED && gap_reads == 3 && adv_stops == 1); // wrap-safe timeout
    reset(); g_start_deadline_ms = 20; gap_result = GAP_CAUSE_FAIL;
    processStart(RadioCommand{0, 23, 7, 0});
    assert(g_state == RadioSvc::FAILED && now_ms == 20 && gap_reads == 2 && adv_stops == 1);
    reset(); observed_adv_state = GAP_ADV_STATE_ADVERTISING; short_write = true;
#if CRABIT_BW16_DIAG_CHECKPOINTS
    processStart(RadioCommand{0, 23, 7, 0});
    assert(g_state == RadioSvc::FAILED && peripheral_begins == 0 && adv_stops == 0 && fail_calls == 1);
#endif
    reset(); observed_adv_state = GAP_ADV_STATE_ADVERTISING; g_peripheral_started = true;
    short_write_call = 2;
#if CRABIT_BW16_DIAG_CHECKPOINTS
    processStart(RadioCommand{0, 23, 7, 0});
    assert(g_state == RadioSvc::FAILED && adv_starts == 1 && adv_stops == 1 && fail_calls == 1);
#endif
    reset(); g_generation++;
    processStart(RadioCommand{0, 23, 7, 0});
    assert(configure_calls == 0 && peripheral_begins == 0);

    reset(); service();
    assert(service_calls == 3 && service_order[0] == 1 && service_order[1] == 2 && service_order[2] == 1);
    reset(); current_task = &foreign_token; service();
    assert(service_calls == 0);
    reset(); g_owner_ready = false; service();
    assert(service_calls == 0);
}
"""
    )


class RadioCheckpointTest(unittest.TestCase):
    def test_start_arriving_after_first_flush_is_sent_before_sdk_entry(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        radio = (ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.cpp").read_text(encoding="utf-8")
        protocol = (ROOT / "firmware/rtl8720dn/NanoUI/Protocol.cpp").read_text(encoding="utf-8")
        fixture = production_fixture(radio, protocol)
        # Keep the real processStart/service/flush/serviceOwner bodies. Replace
        # only fixture stubs; inject UART arrival when the command is dequeued,
        # i.e. after the outer service's initial flush. Configure is the SDK edge.
        extra = r"""
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
enum { UART_STATUS=1, UART_FORWARD=2, UART_COMPANION_STATUS=3 };
enum { COMMAND_START=1, COMMAND_STOP, COMMAND_PAIR_DIAG_BEGIN,
       COMMAND_PAIR_DIAG_CONFIRM_RAM, COMMAND_PAIR_DIAG_CANCEL,
       COMMAND_PAIR_DIAG_END, COMMAND_COMPANION_START };
struct UartEvent { int kind; uint16_t request_id, length; char payload[64]; };
struct BleWriteEvent {};
struct NotifyEvent {};
QueueHandle_t g_uart_queue=reinterpret_cast<void*>(2);
QueueHandle_t g_command_queue=reinterpret_cast<void*>(3);
QueueHandle_t g_event_queue=nullptr, g_notify_queue=nullptr;
bool g_ble_stack_ready=false, arrived=false;
std::vector<UartEvent> pending_uart;
std::vector<std::string> sent_status;
int xQueueReceive(QueueHandle_t, UartEvent* event, unsigned) {
    if (pending_uart.empty()) return 0;
    *event=pending_uart.front(); pending_uart.erase(pending_uart.begin()); return pdTRUE;
}
int xQueueReceive(QueueHandle_t, RadioTraceEvent* event, unsigned) {
    if (async_events.empty()) return 0;
    *event=async_events.front(); async_events.erase(async_events.begin()); return pdTRUE;
}
int xQueueReceive(QueueHandle_t, BleWriteEvent*, unsigned) { return 0; }
int xQueueReceive(QueueHandle_t, NotifyEvent*, unsigned) { return 0; }
int xQueueReceive(QueueHandle_t queue, RadioCommand* command, unsigned) {
    assert(queue==g_command_queue);
    if (arrived) return 0;
    // The outer flush saw nothing. This models UART enqueueing START/QUEUED
    // just before serviceOwner receives its command.
    arrived=true; pending_uart.push_back({UART_STATUS,23,6,"QUEUED"});
    enqueueRadioTrace(TRACE_CMD_RX,23);
    *command=RadioCommand{COMMAND_START,23,7,0}; return pdTRUE;
}
namespace Protocol {
void send_message(const char*, const char* value, uint16_t) { sent_status.push_back(value); }
void send_to_stm32(const char*) {}
}
void processStop(const RadioCommand&) {}
void processPairDiagnosticConfirmRam() {}
void processPairDiagnosticCancel() {}
void processPairDiagnosticEnd() {}
void processCompanion(const RadioCommand&) {}
void handleBleWriteEvent(const BleWriteEvent&) {}
void processNotifyEvent(const NotifyEvent&) {}
void processConnection() {}
void processAuthentication() {}
void processPairing() {}
void attachDiagnostic(const char*, uint16_t) {}
"""
        # Insert after the fake radio types/functions, but before actual start.
        marker = "void flushUartEvents();\nnamespace RadioSvc"
        self.assertIn(marker, fixture)
        fixture = fixture.replace(marker, extra + "\n" + marker, 1)
        fixture = fixture.replace(
            "bool configureBleOnce(uint32_t) { ++configure_calls; return configure_ok; }",
            "bool configureBleOnce(uint32_t);",
            1,
        )
        fixture = fixture.replace(
            "void flushUartEvents() { service_order[service_calls++] = 1; }",
            function(radio, "void serviceRadioTrace(")
            + "\n"
            + function(radio, "void flushUartEvents() {"),
            1,
        )
        fixture = fixture.replace(
            "void serviceOwner() { service_order[service_calls++] = 2; }",
            function(radio, "void serviceOwner()"),
            1,
        )
        # Use a dedicated main; existing cases in the shared fixture exercise
        # the original order-recording doubles and are not this regression.
        fixture = (
            fixture[: fixture.index("int main() {")]
            + r"""
bool configureBleOnce(uint32_t) {
    ++configure_calls;
    assert(arrived && pending_uart.empty() && async_events.empty());
    assert(sent_status.size()==2 && sent_status[0]=="QUEUED" && sent_status[1]=="CMD_RX");
    return false; // stop at the first SDK edge without executing SDK code
}
int main() {
    reset(); service();
    assert(configure_calls==1 && fail_calls==1 && g_state==RadioSvc::FAILED);
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-radio-handoff-") as folder:
            source = Path(folder) / "handoff.cpp"
            binary = Path(folder) / "handoff.exe"
            source.write_text(fixture, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DCRABIT_BW16_DIAG_CHECKPOINTS=1",
                    "-I",
                    str(ROOT / "firmware/shared"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_actual_checkpoint_start_and_service_functions(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required; refusing to skip executable fixture")
        radio = (ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.cpp").read_text(encoding="utf-8")
        protocol = (ROOT / "firmware/rtl8720dn/NanoUI/Protocol.cpp").read_text(encoding="utf-8")
        fixture = production_fixture(radio, protocol)
        with tempfile.TemporaryDirectory(prefix="crabit-radio-checkpoint-") as folder:
            source = Path(folder) / "radio_checkpoint.cpp"
            binary = Path(folder) / "radio_checkpoint.exe"
            source.write_text(fixture, encoding="utf-8")
            for enabled in (0, 1):
                with self.subTest(checkpoints=enabled):
                    subprocess.run(
                        [
                            compiler,
                            "-std=c++11",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            f"-DCRABIT_BW16_DIAG_CHECKPOINTS={enabled}",
                            "-I",
                            str(ROOT / "firmware/shared"),
                            str(source),
                            "-o",
                            str(binary),
                        ],
                        check=True,
                    )
                    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
