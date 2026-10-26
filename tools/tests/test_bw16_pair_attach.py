"""Host regressions for the production BW16 ATTACH-only pairing admission."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RADIO = ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start : i + 1]
    raise AssertionError(f"unterminated function: {signature}")


HARNESS = r"""
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
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
struct Token { int marker=7; size_t length() const { return 0; } };
struct BoundToken { BoundToken& operator=(const char* s) { empty = !s || !*s; return *this; } bool empty=true; };
int MODE_WAITING=1, MODE_BLE=2, current_mode=MODE_BLE;
RadioSvc::State g_state=RadioSvc::ADVERTISING;
RadioSvc::Result g_last_result=RadioSvc::ACQUIRED;
uint32_t g_generation=9, g_start_deadline_ms=0, g_pair_deadline_ms=0;
uint16_t g_request_id=4, g_companion_session=0;
uint8_t g_pair_conn_id=0;
uint32_t g_pending_pair_sequence=0;
uint16_t g_pair_rx_length=17;
bool g_companion_active=false, g_companion_mode_owned=false;
bool g_pair_diagnostic_active=false, ble_is_authenticated=true, g_ble_stack_ready=true;
RadioSvc::PairDiagnosticState g_pair_diagnostic_state=RadioSvc::PAIR_DIAG_OFF;
Token g_pending_pair_token;
BoundToken g_bound_pair_token;
unsigned pair_begin_calls=0, sdk_calls=0, prefs_put_calls=0;
std::vector<std::string> events;
std::vector<std::string> attach_trace;
bool attach_reply_queue_result = true;
void enqueueRadioTrace(int, uint16_t) {}
void attachDiagnostic(const char* stage, uint16_t) { attach_trace.push_back(stage); }
bool enqueueUartEvent(UartEventKind, const char*, uint16_t) { return attach_reply_queue_result; }
void publishCompanionStatus(const char* value, uint16_t id) {
    events.push_back(std::string(value)+":"+std::to_string(id));
}
void publishModePairStatus(const char*, uint32_t=0) {}
void setState(RadioSvc::State value) { g_state=value; }
uint32_t millis() { return 0; }
void processStart(const RadioCommand&) { ++sdk_calls; }
void processStop(const RadioCommand&) { ++sdk_calls; }
void clearPendingPair() { ++sdk_calls; }
void ownerNotify(const char*, uint8_t) { ++sdk_calls; }
void processPairDiagnosticConfirmRam() { ++sdk_calls; }
void processPairDiagnosticCancel() { ++sdk_calls; }
void processPairDiagnosticBegin() { ++pair_begin_calls; g_pair_diagnostic_active=true;
    g_pair_diagnostic_state=RadioSvc::PAIR_DIAG_IDLE; g_pair_rx_length=0; ble_is_authenticated=false; }
"""


class Bw16PairAttachTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = RADIO.read_text(encoding="utf-8")
        cls.compiler = shutil.which("g++")

    def test_real_ram_pair_policy_bypasses_physical_flash_write(self):
        # Execute the actual existing diagnostic/pairing functions, not their
        # admission-fixture stubs. Preferences itself remains a counted double.
        program = r"""
#include <cassert>
#include <cstdint>
#include <string>
namespace RadioSvc {
enum State { IDLE, CONNECTED };
enum PairDiagnosticState { PAIR_DIAG_OFF, PAIR_DIAG_IDLE, PAIR_DIAG_WAITING,
 PAIR_DIAG_RAM_CONFIRMED, PAIR_DIAG_CANCELLED, PAIR_DIAG_TIMEOUT };
bool pairDeadlineExpired(uint32_t now, uint32_t deadline) { return int32_t(now-deadline)>=0; }
}
std::string g_pending_pair_token, g_bound_pair_token;
uint32_t g_pair_deadline_ms=0, g_pending_pair_sequence=0;
uint8_t g_pair_conn_id=0, g_conn_id=1;
uint16_t g_pair_rx_length=0, g_companion_session=0;
bool ble_is_authenticated=false, g_pair_diagnostic_active=false, g_companion_active=false;
bool g_mode_ram_pairing=false;
RadioSvc::State g_state=RadioSvc::CONNECTED;
RadioSvc::PairDiagnosticState g_pair_diagnostic_state=RadioSvc::PAIR_DIAG_OFF;
unsigned prefs_writes=0;
struct Prefs { void putString(const char*, const std::string&) { ++prefs_writes; } } g_pairing_prefs;
const int BTN_OK_PIN=1, LOW=0;
int digitalRead(int) { return LOW; } // Strongest physical-OK trigger.
uint32_t millis() { return 100; }
void ownerNotify(const char*, uint8_t) {}
void publishCompanionStatus(const char*, uint16_t) {}
void publishModePairStatus(const char*, uint32_t=0) {}
"""
        for signature in (
            "void clearPendingPair()",
            "void processPairDiagnosticBegin()",
            "void processPairing()",
            "void processPairDiagnosticConfirmRam()",
        ):
            program += function_body(self.source, signature) + "\n"
        program += r"""
int main() {
 processPairDiagnosticBegin();
 assert(g_pair_diagnostic_active && !ble_is_authenticated);
 g_pending_pair_token="ram-token"; g_pair_conn_id=g_conn_id; g_pair_deadline_ms=1000;
 processPairing();
 assert(prefs_writes==0 && !ble_is_authenticated && !g_pending_pair_token.empty());
 processPairDiagnosticConfirmRam();
 assert(prefs_writes==0 && ble_is_authenticated && g_bound_pair_token=="ram-token");
 assert(g_pending_pair_token.empty() && g_pair_diagnostic_state==RadioSvc::PAIR_DIAG_RAM_CONFIRMED);
 // Control proves the counter detects the unchanged legacy persistence path.
 g_pair_diagnostic_active=false; g_pending_pair_token="legacy"; g_pair_deadline_ms=1000;
 processPairing(); assert(prefs_writes==1 && g_bound_pair_token=="legacy");
}
"""
        with tempfile.TemporaryDirectory(prefix="crabit-ram-pair-policy-") as tmp:
            src, exe = Path(tmp) / "ram.cpp", Path(tmp) / "ram.exe"
            src.write_text(program, encoding="utf-8")
            subprocess.run(
                [
                    self.compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(src),
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            subprocess.run([str(exe)], check=True)

    def test_actual_handler_admission_only_flag_and_legacy_control(self):
        self.assertIsNotNone(self.compiler, "g++ is required for production-body host fixture")
        body = function_body(self.source, "void processCompanion(const RadioCommand& command)")
        attach = body[: body.index("    if (command.kind == COMMAND_COMPANION_START)")]
        self.assertLess(
            body.index("COMMAND_COMPANION_ATTACH"), body.index("COMMAND_COMPANION_START")
        )
        program = (
            HARNESS
            + body
            + r"""
namespace RadioSvc { void flushUartEvents() {} }
static void clean() {
 current_mode=MODE_BLE; g_state=RadioSvc::ADVERTISING; g_generation=9; g_ble_stack_ready=true;
 g_last_result=RadioSvc::ACQUIRED; g_request_id=4; g_start_deadline_ms=0; g_pair_deadline_ms=0;
 g_companion_active=false; g_companion_mode_owned=false; g_companion_session=0;
 g_pair_diagnostic_active=false; g_pair_diagnostic_state=RadioSvc::PAIR_DIAG_OFF;
 g_pair_rx_length=17; g_pair_conn_id=0; g_pending_pair_sequence=0; ble_is_authenticated=true;
 pair_begin_calls=sdk_calls=prefs_put_calls=0; events.clear(); attach_trace.clear();
 attach_reply_queue_result=true; g_bound_pair_token.empty=false;
}
int main() {
 clean(); processCompanion({COMMAND_COMPANION_ATTACH, 31, 9, 0});
#if CRABIT_BW16_DIAG_ATTACH_ADMISSION_ONLY
 assert(events.size()==1 && events[0]=="REJECTED:31");
 assert((attach_trace == std::vector<std::string>{"ATTACH_OWNER", "ATTACH_REJECTED"}));
 assert(!g_companion_active && g_companion_session==0 && !g_companion_mode_owned);
 assert(!g_pair_diagnostic_active && g_pair_diagnostic_state==RadioSvc::PAIR_DIAG_OFF);
 assert(pair_begin_calls==0 && sdk_calls==0 && prefs_put_calls==0);
 assert(!g_bound_pair_token.empty && ble_is_authenticated && g_pair_rx_length==17);
 assert(g_generation==9 && g_pending_pair_sequence==0 && g_pair_conn_id==0);
 // Exercise the other valid radio state with live sentinels. Admission-only
 // must reject without touching pre-existing pairing/session metadata.
 clean(); g_state=RadioSvc::CONNECTED; current_mode=MODE_BLE;
 g_companion_session=88; g_request_id=73; g_start_deadline_ms=4567;
 g_pending_pair_sequence=91; g_pair_conn_id=3; g_pending_pair_token.marker=42;
 g_pair_deadline_ms=7654; g_pair_rx_length=23; ble_is_authenticated=true;
 processCompanion({COMMAND_COMPANION_ATTACH, 52, 9, 0});
 assert(events.size()==1 && events[0]=="REJECTED:52");
 assert(g_state==RadioSvc::CONNECTED && current_mode==MODE_BLE && g_generation==9);
 assert(g_companion_session==88 && !g_companion_active && !g_companion_mode_owned);
 assert(g_request_id==73 && g_start_deadline_ms==4567);
 assert(g_pending_pair_sequence==91 && g_pair_conn_id==3 && g_pending_pair_token.marker==42);
 assert(g_pair_deadline_ms==7654 && g_pair_rx_length==23 && ble_is_authenticated);
 assert(pair_begin_calls==0 && sdk_calls==0 && prefs_put_calls==0);
 // The existing same-session idempotent branch remains ahead of the gate.
 g_companion_active=true; g_companion_session=31; events.clear(); attach_trace.clear();
 processCompanion({COMMAND_COMPANION_ATTACH, 31, 9, 0});
 assert(events.size()==1 && events[0]=="ATTACHED:31" && pair_begin_calls==0);
 g_companion_session=32; events.clear();
 processCompanion({COMMAND_COMPANION_ATTACH, 31, 9, 0});
 assert(events.size()==1 && events[0]=="BUSY:31" && pair_begin_calls==0);
#else
 assert(events.empty());
 assert((attach_trace == std::vector<std::string>{"ATTACH_OWNER", "ATTACH_RAM_BEGIN_DONE", "ATTACH_RAM_READY", "ATTACH_REPLY_QUEUED"}));
 assert(g_companion_active && g_companion_session==31 && !g_companion_mode_owned);
 assert(g_pair_diagnostic_active && g_pair_diagnostic_state==RadioSvc::PAIR_DIAG_IDLE);
 assert(pair_begin_calls==1 && sdk_calls==0 && prefs_put_calls==0);
 assert(g_bound_pair_token.empty && !ble_is_authenticated && g_pair_rx_length==0);
 clean(); attach_reply_queue_result=false; processCompanion({COMMAND_COMPANION_ATTACH, 31, 9, 0});
 assert(g_companion_active && g_companion_session==31);
 assert((attach_trace == std::vector<std::string>{"ATTACH_OWNER", "ATTACH_RAM_BEGIN_DONE", "ATTACH_RAM_READY", "ATTACH_REPLY_REJECTED"}));
 events.clear(); attach_trace.clear(); processCompanion({COMMAND_COMPANION_ATTACH, 31, 9, 0});
 assert(events.size()==1 && events[0]=="ATTACHED:31" && pair_begin_calls==1);
 events.clear(); processCompanion({COMMAND_COMPANION_ATTACH, 32, 9, 0});
 assert(events.size()==1 && events[0]=="BUSY:32" && pair_begin_calls==1);
#endif
 const int modes[]={MODE_WAITING};
 for (int mode : modes) { clean(); current_mode=mode;
   processCompanion({COMMAND_COMPANION_ATTACH, 41, 9, 0});
   assert(events.size()==1 && events[0]=="REJECTED:41" && !g_companion_active && pair_begin_calls==0); }
 clean(); g_generation=10; processCompanion({COMMAND_COMPANION_ATTACH, 41, 9, 0});
 assert(events[0]=="REJECTED:41" && pair_begin_calls==0);
 clean(); g_ble_stack_ready=false; processCompanion({COMMAND_COMPANION_ATTACH, 41, 9, 0});
 assert(events[0]=="REJECTED:41" && pair_begin_calls==0);
 const RadioSvc::State guarded_states[]={RadioSvc::IDLE, RadioSvc::STARTING, RadioSvc::STOPPING};
 for (RadioSvc::State state : guarded_states) {
   clean(); g_state=state; processCompanion({COMMAND_COMPANION_ATTACH, 41, 9, 0});
   assert(events.size()==1 && events[0]=="REJECTED:41" && pair_begin_calls==0); }
 clean(); processCompanion({COMMAND_COMPANION_ATTACH, 41, 9, 1});
 assert(events[0]=="REJECTED:41" && pair_begin_calls==0);
 clean(); processCompanion({COMMAND_COMPANION_ATTACH, 0, 9, 0});
 assert(events[0]=="REJECTED:0" && pair_begin_calls==0);
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-pair-attach-") as tmp:
            src, exe = Path(tmp) / "attach.cpp", Path(tmp) / "attach.exe"
            src.write_text(program, encoding="utf-8")
            for enabled in (1, 0):
                binary = Path(tmp) / f"attach-{enabled}.exe"
                subprocess.run(
                    [
                        self.compiler,
                        "-std=c++11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        f"-DCRABIT_BW16_DIAG_ATTACH_ADMISSION_ONLY={enabled}",
                        str(src),
                        "-o",
                        str(binary),
                    ],
                    check=True,
                )
                subprocess.run([str(binary)], check=True)

    def test_admission_only_config_defaults_on(self):
        config = (ROOT / "firmware/rtl8720dn/NanoUI/config.h").read_text(encoding="utf-8")
        self.assertRegex(
            config,
            r"#ifndef CRABIT_BW16_DIAG_ATTACH_ADMISSION_ONLY\s+#define CRABIT_BW16_DIAG_ATTACH_ADMISSION_ONLY 1\s+#endif",
        )

    def test_attach_diagnostic_stages_are_owner_ordered_and_reply_is_transport_truthful(self):
        owner = function_body(self.source, "void processCompanion(const RadioCommand& command)")
        attach = owner[: owner.index("    if (command.kind == COMMAND_COMPANION_START)")]
        self.assertLess(
            attach.index('attachDiagnostic("ATTACH_OWNER"'),
            attach.index('attachDiagnostic("ATTACH_RAM_READY"'),
        )
        self.assertLess(
            attach.index('attachDiagnostic("ATTACH_RAM_READY"'),
            attach.index('enqueueUartEvent(UART_COMPANION_STATUS, "ATTACHED"'),
        )
        enqueue = function_body(
            self.source,
            "Result enqueueCompanion(uint16_t session_id, const char* action, uint32_t pair_seq)",
        )
        self.assertLess(
            enqueue.index("xQueueSend(g_command_queue, &command, 0)"),
            enqueue.index("enqueueRadioTrace(TRACE_CMD_QUEUED"),
        )
        flush_start = self.source.index("void flushUartEvents() {")
        flush_end = self.source.index("\nvoid service()", flush_start)
        flush = self.source[flush_start:flush_end]
        self.assertIn('strcmp(event.payload, "ATTACHED") == 0', flush)
        self.assertIn('attachDiagnostic("ATTACH_REPLY_BEGIN"', flush)
        self.assertIn('sent ? "ATTACH_REPLY_SENT" : "ATTACH_REPLY_FAIL"', flush)
        self.assertIn(
            'Protocol::send_message_checked("sys/comm", event.payload, event.request_id)', flush
        )

    def test_attach_diagnostic_compile_flag_controls_only_the_trace_send(self):
        helper = function_body(
            self.source, "void attachDiagnostic(const char* stage, uint16_t request_id)"
        )
        program = (
            r"""
#include <cassert>
#include <cstdint>
unsigned sends = 0; const char* topic_seen = nullptr; const char* stage_seen = nullptr; uint16_t id_seen = 0;
namespace Protocol { void send_message(const char* topic, const char* stage, uint16_t id) {
 ++sends; topic_seen=topic; stage_seen=stage; id_seen=id;
} }
"""
            + helper
            + r"""
int main() {
 attachDiagnostic("ATTACH_OWNER", 53);
#if CRABIT_BW16_DIAG_ATTACH_TRACE
 assert(sends==1 && topic_seen && stage_seen && id_seen==53);
#else
 assert(sends==0);
#endif
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-attach-trace-flag-") as folder:
            directory = Path(folder)
            source = directory / "trace_flag.cpp"
            source.write_text(program, encoding="utf-8")
            for enabled in (1, 0):
                binary = directory / f"trace-{enabled}.exe"
                subprocess.run(
                    [
                        self.compiler,
                        "-std=c++11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        f"-DCRABIT_BW16_DIAG_ATTACH_TRACE={enabled}",
                        str(source),
                        "-o",
                        str(binary),
                    ],
                    check=True,
                )
                subprocess.run([str(binary)], check=True)

    def test_enqueue_accepts_attach_only_with_zero_sequence(self):
        body = function_body(
            self.source,
            "Result enqueueCompanion(uint16_t session_id, const char* action, uint32_t pair_seq)",
        )
        self.assertIn('!strcmp(action, "ATTACH")', body)
        self.assertIn("(!pairing_action && pair_seq != 0)", body)
        self.assertIn("xQueueSend(g_command_queue, &command, 0)", body)
        rejected = body[body.index("if (xQueueSend(g_command_queue, &command, 0) != pdTRUE)") :]
        self.assertIn("enqueueRadioTrace(TRACE_CMD_REJECTED, session_id)", rejected)
        self.assertLess(
            rejected.index("enqueueRadioTrace(TRACE_CMD_REJECTED"),
            rejected.index('publishCompanionStatus("REJECTED"'),
        )
        self.assertLess(body.index('!strcmp(action, "ATTACH")'), body.index("xQueueSend"))
        fixture = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstring>
enum Result { QUEUED, ACQUIRED, BUSY, REJECTED };
const int pdTRUE=1;
enum CommandKind { COMMAND_START=1, COMMAND_STOP, COMMAND_PAIR_DIAG_BEGIN,
 COMMAND_PAIR_DIAG_CONFIRM_RAM, COMMAND_PAIR_DIAG_CANCEL, COMMAND_PAIR_DIAG_END,
 COMMAND_COMPANION_START, COMMAND_COMPANION_STOP, COMMAND_COMPANION_CONFIRM,
 COMMAND_COMPANION_CANCEL, COMMAND_COMPANION_ATTACH };
struct RadioCommand { CommandKind kind; uint16_t request_id; uint32_t generation; uint32_t pair_seq; };
struct Queue {};
Queue queue; Queue* g_command_queue=&queue; bool g_owner_ready=true;
uint32_t g_generation=55; bool queue_accept=true; RadioCommand last{}; unsigned sends=0;
const char* last_status="";
int xQueueSend(Queue*, const RadioCommand* cmd, int) { ++sends; if (!queue_accept) return 0; last=*cmd; return 1; }
namespace RadioSvc {
void flushUartEvents() {}
}
void publishCompanionStatus(const char* status, uint16_t) { last_status=status; }
namespace RadioSvc {
"""
            + body
            + r"""
}
int main() {
 using namespace RadioSvc;
 assert(enqueueCompanion(77,"ATTACH",0)==QUEUED);
 assert(last.kind==COMMAND_COMPANION_ATTACH && last.request_id==77 && last.generation==55 && last.pair_seq==0);
 assert(enqueueCompanion(78,"ATTACH",1)==REJECTED && sends==1);
 assert(enqueueCompanion(0,"ATTACH",0)==REJECTED && sends==1);
 queue_accept=false;
 assert(enqueueCompanion(79,"ATTACH",0)==REJECTED && sends==2 && strcmp(last_status,"REJECTED")==0);
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-enqueue-") as tmp:
            src, exe = Path(tmp) / "enqueue.cpp", Path(tmp) / "enqueue.exe"
            src.write_text(fixture, encoding="utf-8")
            subprocess.run(
                [
                    self.compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(src),
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
