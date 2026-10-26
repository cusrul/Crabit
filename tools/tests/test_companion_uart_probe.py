"""Run the probe route and production observation/status functions on host."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
    raise AssertionError("unterminated function")


class ProbeTest(unittest.TestCase):
    def test_product_default_and_explicit_probe_build(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        service = (ROOT / "firmware/stm32/Core/Src/companion_ble.cpp").read_text(encoding="utf-8")
        port = (ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp").read_text(encoding="utf-8")
        self.assertIn("sys_cfg.enable_ble && Crabit::CompanionBle::legacyAutostartAllowed()", port)
        self.assertIn(
            "EventBus::tryPublish(CompanionProbe::topic(), CompanionProbe::value(), state.session)",
            service,
        )
        receive = function(service, "void receive(")
        self.assertIn("if (CompanionProbe::Enabled) return;", receive)
        self.assertIn("if (modeActive()) {", receive)
        self.assertIn("if (!CompanionProbe::Enabled) state.stopSent", service)
        fixture = r"""
#include "companion_probe.h"
#include "companion_session.h"
#include <cassert>
using namespace Crabit;
namespace Crabit { namespace CompanionTrace {
enum Event { ProbePongAccepted };
unsigned observed = 0;
void record(Event, uint16_t) { ++observed; }
} }
CompanionSession state;
bool probe_pong = false, legacy_active = false;
enum class ModePhase { Off, Starting, Running, StopRequested, Stopping, Failed };
ModePhase mode_phase = ModePhase::Off;
const char* mode_status = "OFF";
"""
        fixture += function(service, "void observeProbePong(") + "\n"
        fixture += function(service, "bool legacyAutostartAllowed()") + "\n"
        fixture += (
            function(service, "const char* status()")
            + r"""
int main() {
    using namespace CompanionProbe;
    assert(legacyAutostartAllowed() == !(Enabled || CRABIT_COMPANION_SKIP_LEGACY_AUTOSTART));
    assert(strcmp(topic(), Enabled ? "sys/bw/ping" : "sys/comm") == 0);
    assert(strcmp(value(), Enabled ? "PING" : "START") == 0);
    observeProbePong("BW16 PONG OK!"); assert(!probe_pong);
    assert(state.start(7)); state.tick(0); state.booted(100);
    assert(state.tick(1300) == CompanionSession::Action::PrepareAndStart);
    state.started(true, 1300);
    observeProbePong("BW16 PONG OK!extra"); assert(!probe_pong);
    observeProbePong(nullptr); assert(!probe_pong);
    observeProbePong("BW16 PONG OK!"); assert(probe_pong == Enabled);
    assert(CompanionTrace::observed == (Enabled ? 1U : 0U));
    assert(state.phase == CompanionSession::Phase::Starting);
    assert(strcmp(status(), Enabled ? "PONG SEEN" : "WAIT BLE") == 0);
    assert(state.tick(8299) == CompanionSession::Action::None);
    assert(state.tick(8300) == CompanionSession::Action::PowerOff);
    state.poweredOff();
    assert(strcmp(status(), Enabled ? "PONG SEEN / OFF" : "START TIMEOUT") == 0);
    assert(state.start(8)); probe_pong = false;
    state.tick(9000); state.booted(9000); state.tick(10200); state.started(true, 10200);
    state.tick(17200); state.poweredOff();
    observeProbePong("BW16 PONG OK!"); assert(!probe_pong);
    assert(strcmp(status(), Enabled ? "PING TIMEOUT" : "START TIMEOUT") == 0);
    const uint8_t bytes[] = {0, 255, 32, 126, 31, 127};
    auto summary = summarize(bytes, sizeof(bytes));
    assert(summary.size == 6 && summary.zero == 1 && summary.ff == 1 && summary.printable == 2);
    uint8_t zeros[511] = {};
    summary = summarize(zeros, 9999);
    assert(summary.size == 511 && summary.zero == 511 && summary.ff == 0 && summary.printable == 0);
    summary = summarize(nullptr, 9999); assert(summary.size == 0);
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-uart-probe-") as folder:
            source = Path(folder) / "probe.cpp"
            binary = Path(folder) / "probe.exe"
            source.write_text(fixture, encoding="utf-8")
            for flag, skip in ((0, 0), (1, 0), (0, 1), (1, 1)):
                subprocess.run(
                    [
                        compiler,
                        "-std=c++11",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        f"-DCRABIT_COMPANION_UART_PROBE_ONLY={flag}",
                        f"-DCRABIT_COMPANION_SKIP_LEGACY_AUTOSTART={skip}",
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
