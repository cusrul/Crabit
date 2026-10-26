"""Focused source contract checks for the BW16 headless companion lane."""

from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
radio = (ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.cpp").read_text(encoding="utf-8")
header = (ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.h").read_text(encoding="utf-8")
entry = (ROOT / "firmware/rtl8720dn/NanoUI/src/main_entry.cpp").read_text(encoding="utf-8")
services = (ROOT / "firmware/rtl8720dn/NanoUI/AmebaServices.h").read_text(encoding="utf-8")

assert "enqueueCompanion(uint16_t session_id" in header
assert 'strcmp(message.topic, "sys/comm") == 0' in entry
assert "AmebaServices::enqueueCompanion(message)" in entry
assert "RadioSvc::enqueueCompanion(message.request_id, action, sequence)" in services
assert "current_mode != MODE_WAITING" in radio
assert "COMMAND_COMPANION_STOP" in radio and "processStop" in radio
assert "command.request_id != g_companion_session" in radio
assert "command.pair_seq != g_pending_pair_sequence" in radio
assert 'publishCompanionStatus("PAIR_REJECTED", command.request_id)' in radio
assert "RadioSvc::pairDeadlineExpired(millis(), g_pair_deadline_ms)" in radio
companion_owner = radio[radio.index("void processCompanion(const RadioCommand& command)") :]
confirm_block = companion_owner[
    companion_owner.index(
        "if (RadioSvc::pairDeadlineExpired(millis(), g_pair_deadline_ms)"
    ) : companion_owner.index(
        "if (command.kind == COMMAND_COMPANION_CONFIRM) processPairDiagnosticConfirmRam()"
    )
]
assert 'publishCompanionStatus("PAIR_TIMEOUT", g_companion_session)' in confirm_block
assert "clearPendingPair();" in confirm_block
assert "processPairDiagnosticConfirmRam()" not in confirm_block
assert "g_pending_pair_sequence = 0" in radio
assert 'Protocol::send_message("sys/comm", event.payload, event.request_id)' in radio
assert 'Protocol::send_message("sys/comm", "REJECTED"' not in entry
assert 'strcmp(message.topic, "sys/settings") == 0' in entry
assert 'strcmp(message.topic, "sys/settings/set") == 0' in entry
assert 'strcmp(message.topic, "app/badusb") == 0' in entry
assert (
    "g_pairing_prefs.putString"
    not in radio[
        radio.index("void processPairDiagnosticConfirmRam") : radio.index(
            "void processPairDiagnosticCancel"
        )
    ]
)
assert 'publishCompanionStatus("AUTHENTICATED", g_companion_session)' in radio
assert "g_start_deadline_ms = millis() + RADIO_START_TIMEOUT_MS" in radio
assert "g_generation = command.generation" in radio
assert (
    "g_pending_pair_sequence = 0;"
    in radio[radio.index("void clearPendingPair") : radio.index("bool ownerNotify")]
)
assert "companionControlsMode()" in services
assert "millis() - boot_time > 5000" not in entry
assert 'Protocol::send_message("diag/radio", "BOOT_READY")' in entry
assert "current_mode = MODE_BLE;" in companion_owner
compiler = shutil.which("g++")
assert compiler, "g++ is required for the executable host regression"
with tempfile.TemporaryDirectory(prefix="crabit-companion-test-") as output:
    executable = Path(output) / "companion-control-host.exe"
    source = ROOT / "tools/tests/companion_control_host.cpp"
    fake_include = ROOT / "tools/tests/fake_arduino"
    subprocess.run(
        [compiler, "-std=c++11", "-I", str(fake_include), str(source), "-o", str(executable)],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
    # Execute the actual first-write handler with SDK-free doubles. A first
    # write may precede connection polling; STM32 must learn CONNECTED first.
    handler = radio[
        radio.index("void handleBleWriteEvent(") : radio.index("void processNotifyEvent(")
    ]
    fixture = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace RadioSvc { enum State { ADVERTISING, CONNECTED, STOPPED }; }
namespace NetManager { uint8_t active_conn_id = 0; }
struct BleWriteEvent { uint8_t conn_id; unsigned length; const char* data; };
RadioSvc::State g_state = RadioSvc::ADVERTISING;
bool ble_was_connected = false, g_companion_active = true, g_pair_diagnostic_active = true;
uint8_t g_conn_id = 0;
uint16_t g_request_id = 7, g_companion_session = 9, g_pair_rx_length = 0;
constexpr unsigned RADIO_RX_BUFFER_MAX = 128;
unsigned g_rx_length = 0;
char g_rx_buffer[RADIO_RX_BUFFER_MAX + 1] = {};
std::vector<std::string> events;
void setState(RadioSvc::State state) { g_state = state; }
void publishBleStatus(const char*, uint16_t) {}
void publishCompanionStatus(const char* status, uint16_t session) {
    assert(session == 9); events.push_back(status);
}
bool bleCommandIsComplete() { return g_rx_length >= 9; }
void handleBleCommand(const char*, uint8_t conn) {
    assert(conn == 3); events.push_back("PAIR_WAIT");
}
"""
    fixture += (
        handler
        + r"""
int main() {
    handleBleWriteEvent({3, 7, "PAIR:ab"});
    assert(events.size() == 1 && events[0] == "CONNECTED");
    handleBleWriteEvent({3, 2, "cd"});
    assert(events.size() == 2 && events[1] == "PAIR_WAIT");
    assert(g_pair_rx_length == 9 && ble_was_connected);
    events.clear(); g_state = RadioSvc::STOPPED;
    handleBleWriteEvent({3, 9, "PAIR:abcd"});
    assert(events.empty());
    g_state = RadioSvc::ADVERTISING; g_companion_active = false;
    handleBleWriteEvent({3, 9, "PAIR:abcd"});
    assert(events.size() == 1 && events[0] == "PAIR_WAIT");
}
"""
    )
    handler_source = Path(output) / "first-write.cpp"
    handler_source.write_text(fixture, encoding="utf-8")
    subprocess.run(
        [
            compiler,
            "-std=c++11",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(handler_source),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
print("companion control protocol source checks passed")
