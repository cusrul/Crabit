"""Execute BW16's mode-BLE confirmation owner path with narrow host doubles."""

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
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class Bw16ModeRamPairTests(unittest.TestCase):
    def test_actual_owner_confirmation_rejection_timeout_and_ram_only_result(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for production-body host fixture")
        radio = RADIO.read_text(encoding="utf-8")
        bodies = [
            function_body(radio, "void clearPendingPair()"),
            function_body(radio, "void publishModePairStatus("),
            function_body(radio, "void processPairDiagnosticConfirmRam()"),
            function_body(radio, "void processPairDiagnosticCancel()"),
        ]
        confirm = function_body(radio, "Result confirmModePair(")
        fixture = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace RadioSvc {
enum State { IDLE, STARTING, ADVERTISING, CONNECTED, STOPPING, FAILED, TIMEOUT };
enum Result { QUEUED, ACQUIRED, BUSY, REJECTED, FAILED_RESULT, STOPPED_RESULT, TIMEOUT_RESULT };
enum PairDiagnosticState { PAIR_DIAG_OFF, PAIR_DIAG_IDLE, PAIR_DIAG_WAITING,
    PAIR_DIAG_RAM_CONFIRMED, PAIR_DIAG_CANCELLED, PAIR_DIAG_TIMEOUT };
inline bool pairDeadlineExpired(uint32_t now, uint32_t deadline) {
    return int32_t(now - deadline) >= 0;
}
}
enum { MODE_BLE=2, MODE_UI=3 };
struct Event { std::string value; uint16_t id; };
std::vector<Event> events;
void publishCompanionStatus(const char* value, uint16_t id) { events.push_back({value, id}); }
std::string g_pending_pair_token, g_bound_pair_token;
uint32_t g_pair_deadline_ms=0, g_pending_pair_sequence=0;
uint32_t g_pair_sequence=0;
uint8_t g_pair_conn_id=0, g_conn_id=9;
uint16_t g_pair_rx_length=10, g_mode_pair_session=77;
int current_mode=MODE_BLE;
bool g_mode_ram_pairing=true, ble_is_authenticated=false, g_pair_diagnostic_active=true;
bool g_companion_active=false;
RadioSvc::State g_state=RadioSvc::CONNECTED;
RadioSvc::PairDiagnosticState g_pair_diagnostic_state=RadioSvc::PAIR_DIAG_WAITING;
void* g_loop_owner=reinterpret_cast<void*>(1);
void* active_task=reinterpret_cast<void*>(1);
void* xTaskGetCurrentTaskHandle() { return active_task; }
uint32_t now_ms=100;
uint32_t millis() { return now_ms; }
unsigned prefs_writes=0, owner_notifications=0;
struct Prefs { void putString(const char*, const std::string&) { ++prefs_writes; } } g_pairing_prefs;
void ownerNotify(const char*, uint8_t) { ++owner_notifications; }
struct Ble { bool link=true; uint8_t connected_id=9;
 bool connected(uint8_t id) const { return link && id==connected_id; }
} BLE;
"""
            + "\n".join(bodies)
            + "\nnamespace RadioSvc {\n"
            + confirm
            + "\n}\n"
            + r"""
static void reset_case() {
    events.clear();
    g_pending_pair_token="candidate-token"; g_bound_pair_token="";
    g_pair_deadline_ms=30100; g_pending_pair_sequence=23; g_pair_sequence=23;
    g_pair_conn_id=g_conn_id=9; g_pair_rx_length=10; g_mode_pair_session=77;
    current_mode=MODE_BLE;
    g_mode_ram_pairing=true; ble_is_authenticated=false; g_pair_diagnostic_active=true;
    g_companion_active=false; g_state=RadioSvc::CONNECTED;
    g_pair_diagnostic_state=RadioSvc::PAIR_DIAG_WAITING;
    g_loop_owner=active_task=reinterpret_cast<void*>(1); now_ms=100;
    prefs_writes=owner_notifications=0; BLE.link=true; BLE.connected_id=9;
}
static void rejected_without_auth() {
    assert(!ble_is_authenticated && g_pending_pair_token=="candidate-token");
    assert(prefs_writes==0 && g_bound_pair_token.empty());
}
int main() {
    // Only the recorded Arduino owner can reach the synchronous SDK path.
    reset_case(); active_task=reinterpret_cast<void*>(2);
    assert(RadioSvc::confirmModePair(77,"CONFIRM",23)==RadioSvc::REJECTED);
    rejected_without_auth(); assert(events.empty());

    // Stale session, stale/zero sequence, invalid action, wrong mode or no RAM
    // lock are all non-authenticating rejects.
    reset_case(); assert(RadioSvc::confirmModePair(76,"CONFIRM",23)==RadioSvc::REJECTED);
    rejected_without_auth(); assert(events.empty());
    reset_case(); assert(RadioSvc::confirmModePair(77,"CONFIRM",22)==RadioSvc::REJECTED);
    rejected_without_auth(); assert(events.size()==1 && events.back().id==77 && events.back().value=="PAIR_REJECTED:22");
    reset_case(); assert(RadioSvc::confirmModePair(77,"CONFIRM",0)==RadioSvc::REJECTED);
    rejected_without_auth(); assert(events.empty());
    reset_case(); assert(RadioSvc::confirmModePair(77,"ATTACH",23)==RadioSvc::REJECTED);
    rejected_without_auth();
    reset_case(); g_mode_ram_pairing=false;
    assert(RadioSvc::confirmModePair(77,"CONFIRM",23)==RadioSvc::REJECTED); rejected_without_auth();
    reset_case(); g_mode_pair_session=0;
    assert(RadioSvc::confirmModePair(77,"CONFIRM",23)==RadioSvc::REJECTED); rejected_without_auth();

    // Link identity/state and BLE's own connected(conn_id) must all agree.
    reset_case(); g_state=RadioSvc::ADVERTISING;
    assert(RadioSvc::confirmModePair(77,"CONFIRM",23)==RadioSvc::REJECTED); rejected_without_auth();
    reset_case(); g_pair_conn_id=8;
    assert(RadioSvc::confirmModePair(77,"CONFIRM",23)==RadioSvc::REJECTED); rejected_without_auth();
    reset_case(); BLE.link=false;
    assert(RadioSvc::confirmModePair(77,"CONFIRM",23)==RadioSvc::REJECTED); rejected_without_auth();

    // Deadline is exclusive: at the deadline the pending token expires and
    // confirmation cannot authenticate or persist it.
    reset_case(); now_ms=g_pair_deadline_ms;
    assert(RadioSvc::confirmModePair(77,"CONFIRM",23)==RadioSvc::TIMEOUT_RESULT);
    assert(!ble_is_authenticated && g_pending_pair_token.empty() && prefs_writes==0);
    assert(g_pair_diagnostic_state==RadioSvc::PAIR_DIAG_TIMEOUT);
    assert(events.size()==1 && events.back().id==77 && events.back().value=="PAIR_TIMEOUT:23");

    // Cancel rejects the candidate without writing Preferences.
    reset_case();
    assert(RadioSvc::confirmModePair(77,"CANCEL",23)==RadioSvc::STOPPED_RESULT);
    assert(!ble_is_authenticated && g_pending_pair_token.empty() && prefs_writes==0);
    assert(g_pair_diagnostic_state==RadioSvc::PAIR_DIAG_CANCELLED);
    assert(events.size()==1 && events.back().id==77 && events.back().value=="PAIR_CANCELLED:23");

    // Confirm is RAM-only and consumes the sequence, so replay is rejected.
    reset_case();
    assert(RadioSvc::confirmModePair(77,"CONFIRM",23)==RadioSvc::ACQUIRED);
    assert(ble_is_authenticated && g_bound_pair_token=="candidate-token");
    assert(g_pending_pair_token.empty() && g_pending_pair_sequence==0 && prefs_writes==0);
    assert(g_pair_diagnostic_state==RadioSvc::PAIR_DIAG_RAM_CONFIRMED);
    assert(events.size()==1 && events.back().id==77 && events.back().value=="AUTHENTICATED:23");
    assert(RadioSvc::confirmModePair(77,"CONFIRM",23)==RadioSvc::REJECTED);
    assert(ble_is_authenticated && prefs_writes==0);

    // The status helper emits only for a valid active mode session and keeps
    // the session in request_id while sequence stays in the value.
    reset_case(); events.clear();
    publishModePairStatus("PAIR_WAIT", 99);
    assert(events.size()==1 && events.back().id==77 && events.back().value=="PAIR_WAIT:99");
    publishModePairStatus("CONNECTED");
    assert(events.size()==2 && events.back().id==77 && events.back().value=="CONNECTED");
    g_mode_pair_session=0; publishModePairStatus("PAIR_WAIT", 100);
    g_mode_pair_session=77; g_mode_ram_pairing=false; publishModePairStatus("PAIR_WAIT", 101);
    g_mode_ram_pairing=true; current_mode=MODE_UI; publishModePairStatus("PAIR_WAIT", 102);
    assert(events.size()==2);
    return 0;
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-mode-pair-") as folder:
            source = Path(folder) / "mode_pair.cpp"
            binary = Path(folder) / "mode_pair.exe"
            source.write_text(fixture, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr + "\n" + source.read_text())


if __name__ == "__main__":
    unittest.main()
