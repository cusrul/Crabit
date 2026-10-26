"""Executable regression for the temporary BW16 companion START admission gate."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RADIO_PATH = ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class AdmissionOnlyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.radio = RADIO_PATH.read_text(encoding="utf-8")
        cls.compiler = shutil.which("g++")

    def test_actual_handler_with_diagnostic_enabled_and_disabled(self):
        self.assertIsNotNone(self.compiler, "g++ is required for the source-extracted regression")
        self.assertIn("#define CRABIT_BW16_DIAG_START_ADMISSION_ONLY 1", self.radio)
        handler = function_body(self.radio, "void processCompanion(const RadioCommand& command)")
        start_only = handler[: handler.index("    if (!g_companion_active ||")]
        gate = start_only.index("#if CRABIT_BW16_DIAG_START_ADMISSION_ONLY")
        self.assertGreater(
            gate, start_only.index("if (g_companion_active || g_state != RadioSvc::IDLE)")
        )
        self.assertLess(gate, start_only.index("g_generation = command.generation"))

        fixture = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#ifndef CRABIT_BW16_DIAG_START_ADMISSION_ONLY
#define CRABIT_BW16_DIAG_START_ADMISSION_ONLY 1
#endif
namespace RadioSvc {
enum State { IDLE, STARTING, ADVERTISING, CONNECTED, STOPPING, FAILED, TIMEOUT };
enum Result { QUEUED, ACQUIRED, BUSY, REJECTED, FAILED_RESULT, STOPPED_RESULT, TIMEOUT_RESULT };
enum PairDiagnosticState { PAIR_DIAG_OFF, PAIR_DIAG_IDLE, PAIR_DIAG_WAITING,
    PAIR_DIAG_RAM_CONFIRMED, PAIR_DIAG_CANCELLED, PAIR_DIAG_TIMEOUT };
inline bool pairDeadlineExpired(uint32_t now, uint32_t deadline) { return int32_t(now-deadline) >= 0; }
void flushUartEvents();
}
enum CommandKind { COMMAND_START, COMMAND_STOP, COMMAND_PAIR_DIAG_BEGIN,
    COMMAND_PAIR_DIAG_CONFIRM_RAM, COMMAND_PAIR_DIAG_CANCEL, COMMAND_PAIR_DIAG_END,
    COMMAND_COMPANION_START, COMMAND_COMPANION_STOP, COMMAND_COMPANION_CONFIRM,
    COMMAND_COMPANION_CANCEL, COMMAND_COMPANION_ATTACH };
enum RadioTraceStage { TRACE_START_OWNER, TRACE_CMD_QUEUED, TRACE_CMD_REJECTED };
enum UartEventKind { UART_STATUS, UART_COMPANION_STATUS, UART_FORWARD };
const uint32_t RADIO_START_TIMEOUT_MS = 5000;
struct RadioCommand { CommandKind kind; uint16_t request_id; uint32_t generation; uint32_t pair_seq; };
struct Token { size_t length() const { return 0; } };
struct BoundToken { BoundToken& operator=(const char*) { return *this; } };
int MODE_WAITING = 1, MODE_BLE = 2;
int current_mode = MODE_WAITING;
RadioSvc::State g_state = RadioSvc::IDLE;
RadioSvc::Result g_last_result = RadioSvc::STOPPED_RESULT;
uint32_t g_generation = 4, g_start_deadline_ms = 0, g_pair_deadline_ms = 0;
uint16_t g_request_id = 17, g_companion_session = 23;
uint8_t g_pair_conn_id = 0;
uint32_t g_pending_pair_sequence = 0;
uint16_t g_pair_rx_length = 0;
bool g_companion_active = false, g_companion_mode_owned = false;
bool g_pair_diagnostic_active = false, ble_is_authenticated = false, g_ble_stack_ready = false;
BoundToken g_bound_pair_token;
unsigned pair_begin_calls = 0;
RadioSvc::PairDiagnosticState g_pair_diagnostic_state = RadioSvc::PAIR_DIAG_OFF;
Token g_pending_pair_token;
unsigned start_calls = 0, sdk_calls = 0;
std::vector<std::string> events;
void enqueueRadioTrace(int, uint16_t) {}
void attachDiagnostic(const char*, uint16_t) {}
bool enqueueUartEvent(UartEventKind, const char*, uint16_t) { return true; }
void publishCompanionStatus(const char* value, uint16_t id) {
    events.push_back(std::string("status:") + value + ":" + std::to_string(id));
}
void setState(RadioSvc::State state) { g_state = state; }
uint32_t millis() { return 0; }
void processStart(const RadioCommand&) { ++start_calls; ++sdk_calls; g_state = RadioSvc::ADVERTISING; }
void processStop(const RadioCommand&) {}
void clearPendingPair() {}
void ownerNotify(const char*, uint8_t) {}
void processPairDiagnosticConfirmRam() {}
void processPairDiagnosticCancel() {}
void processPairDiagnosticBegin() { ++pair_begin_calls; g_pair_diagnostic_active = true; }
"""
        fixture += handler
        fixture += r"""
namespace RadioSvc { void flushUartEvents() { events.push_back("flush"); } }
static void reset() {
    current_mode = MODE_WAITING; g_state = RadioSvc::IDLE;
    g_last_result = RadioSvc::STOPPED_RESULT; g_generation = 4;
    g_request_id = 17; g_companion_session = 23; g_start_deadline_ms = 0;
    g_companion_active = false; g_companion_mode_owned = false;
    g_pair_diagnostic_active = false; start_calls = sdk_calls = 0; events.clear();
}
int main() {
    reset();
    processCompanion({COMMAND_COMPANION_START, 71, 5, 0});
#if CRABIT_BW16_DIAG_START_ADMISSION_ONLY
    assert(g_last_result == RadioSvc::REJECTED);
    assert(events.size() == 2 && events[0] == "status:REJECTED:71" && events[1] == "flush");
    assert(start_calls == 0 && sdk_calls == 0);
    assert(current_mode == MODE_WAITING && g_state == RadioSvc::IDLE && g_generation == 4);
    assert(g_request_id == 17 && !g_companion_active && !g_companion_mode_owned &&
           g_companion_session == 23 && !g_pair_diagnostic_active);
#else
    assert(start_calls == 1 && sdk_calls == 1);
    assert(current_mode == MODE_BLE && g_state == RadioSvc::ADVERTISING && g_generation == 5);
    assert(g_companion_active && g_companion_session == 71);
    assert(events.size() == 1 && events[0] == "status:READY:71");
#endif
    reset(); current_mode = MODE_BLE;
    processCompanion({COMMAND_COMPANION_START, 71, 5, 0});
    assert(events.size() == 1 && events[0] == "status:REJECTED:71");
    assert(g_generation == 4 && start_calls == 0 && g_state == RadioSvc::IDLE);
    reset(); g_companion_active = true;
    processCompanion({COMMAND_COMPANION_START, 71, 5, 0});
    assert(events.size() == 1 && events[0] == "status:BUSY:71");
    assert(g_generation == 4 && start_calls == 0);
    reset(); g_state = RadioSvc::STARTING;
    processCompanion({COMMAND_COMPANION_START, 71, 5, 0});
    assert(events.size() == 1 && events[0] == "status:BUSY:71");
    assert(g_generation == 4 && start_calls == 0);
    reset();
    processCompanion({COMMAND_COMPANION_START, 71, 6, 0});
    assert(events.size() == 1 && events[0] == "status:REJECTED:71");
    assert(g_generation == 4 && start_calls == 0);
}
"""
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-admission-") as output:
            source = Path(output) / "admission.cpp"
            source.write_text(fixture, encoding="utf-8")
            for enabled in (1, 0):
                executable = Path(output) / f"admission-{enabled}.exe"
                subprocess.run(
                    [
                        self.compiler,
                        "-std=c++11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        f"-DCRABIT_BW16_DIAG_START_ADMISSION_ONLY={enabled}",
                        str(source),
                        "-o",
                        str(executable),
                    ],
                    check=True,
                )
                subprocess.run([str(executable)], check=True)

    def test_rejected_reply_is_start_failure_and_stale_reply_is_ignored(self):
        companion = (ROOT / "firmware/shared/companion_session.h").read_text(encoding="utf-8")
        body = function_body(
            companion, "void receive(uint16_t id, const char* value, uint32_t now = 0)"
        )
        self.assertIn('strcmp(value, "REJECTED") == 0) fail("BLE START FAILED")', body)
        self.assertIn("if (!value || id != session || !active()) return;", body)
        fixture = r"""
#include <cassert>
#include <cstring>
#include <string>
#include "firmware/shared/companion_session.h"
int main() {
    Crabit::CompanionSession session;
    assert(session.start(71));
    assert(session.tick(0) == Crabit::CompanionSession::Action::Boot);
    session.booted(10);
    assert(session.tick(1210) == Crabit::CompanionSession::Action::PrepareAndStart);
    session.started(true, 1210);
    session.receive(70, "REJECTED", 1211);
    assert(session.phase == Crabit::CompanionSession::Phase::Starting);
    assert(std::strcmp(session.status, "WAIT BLE") == 0);
    session.receive(71, "REJECTED", 1212);
    assert(std::strcmp(session.status, "BLE START FAILED") == 0);
    assert(session.phase == Crabit::CompanionSession::Phase::PowerOffRequested);
    assert(session.tick(1213) == Crabit::CompanionSession::Action::PowerOff);
}
"""
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-session-") as output:
            source = Path(output) / "session.cpp"
            executable = Path(output) / "session.exe"
            source.write_text(fixture, encoding="utf-8")
            subprocess.run(
                [
                    self.compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT),
                    str(source),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_ui_start_route_is_not_conditioned_on_diagnostic_macro(self):
        net = (ROOT / "firmware/rtl8720dn/NanoUI/NetManager.cpp").read_text(encoding="utf-8")
        entry = (ROOT / "firmware/rtl8720dn/NanoUI/src/main_entry.cpp").read_text(encoding="utf-8")
        request = function_body(net, "RadioSvc::Result initBLE(uint16_t request_id)")
        self.assertIn("return RadioSvc::requestStart(request_id);", request)
        self.assertNotIn("CRABIT_BW16_DIAG_START_ADMISSION_ONLY", request)
        radio_request = function_body(self.radio, "Result requestStart(uint16_t request_id)")
        self.assertNotIn("CRABIT_BW16_DIAG_START_ADMISSION_ONLY", radio_request)
        self.assertIn('strcmp(message.topic, "sys/comm") == 0', entry)


if __name__ == "__main__":
    unittest.main()
