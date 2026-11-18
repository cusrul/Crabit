"""Correlated BW16 UI-ready handshake: host state and integration guards."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "firmware/stm32/Core/NanoUI/app_real/app_cyber_link.h"
SERVICES = ROOT / "firmware/stm32/Core/Src/app_services.cpp"
BUS = ROOT / "firmware/stm32/Core/Inc/event_bus.h"
LINK = ROOT / "firmware/stm32/Core/Inc/bw16_ui_link.h"
STARTUP = ROOT / "firmware/stm32/Core/Src/startup.cpp"
BW16_SERVICES = ROOT / "firmware/rtl8720dn/NanoUI/AmebaServices.h"
BW16_ENTRY = ROOT / "firmware/rtl8720dn/NanoUI/src/main_entry.cpp"


class Bw16UiReadyTests(unittest.TestCase):
    def test_correlation_state_rejects_stale_and_legacy_acks(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "an existing local g++ is required")
        source = r"""
#include "bw16_ui_link.h"
#include <cassert>
#include <cstdio>
using namespace Crabit::Bw16Ui;
int main() {
    cancel();
    assert(status() == Status::Idle && requestId() == 0 && failure() == nullptr);

    const uint16_t first = begin();
    assert(first >= 0x8000u && requestId() == first && status() == Status::Waiting);
    // The legacy request_id == 0 path can never complete the new handshake.
    assert(!acceptAck(0, "UI_READY"));
    assert(status() == Status::Waiting);
    // A stale or foreign id is ignored.
    assert(!acceptAck((uint16_t)(first + 1), "UI_READY"));
    assert(status() == Status::Waiting);
    // Same id but an unrelated value is not consumed by this contract.
    assert(!acceptAck(first, "WIFI_OK"));
    assert(status() == Status::Waiting);
    assert(acceptAck(first, "UI_READY"));
    assert(status() == Status::Ready);
    // Duplicate answers cannot downgrade a successful claim.
    assert(acceptAck(first, "REJECTED"));
    assert(status() == Status::Ready);

    const uint16_t second = begin();
    assert(second != 0 && second != first && status() == Status::Waiting);
    assert(!acceptAck(first, "UI_READY")); // previous request is stale now
    assert(status() == Status::Waiting);
    assert(acceptAck(second, "REJECTED"));
    assert(status() == Status::Failed && failure() != nullptr);
    assert(std::string(failure()) == "UI REJECTED");

    const uint16_t third = begin();
    assert(acceptAck(third, "BUSY"));
    assert(status() == Status::Failed && std::string(failure()) == "UI BUSY");
    const uint16_t fourth = begin();
    assert(acceptAck(fourth, "UI_FAILED"));
    assert(status() == Status::Failed && std::string(failure()) == "UI NOT READY");

    cancel();
    assert(status() == Status::Idle && requestId() == 0 && failure() == nullptr);
    puts("BW16 UI ready correlation checks passed");
    return 0;
}
"""
        source = source.replace('#include <cstdio>', '#include <cstdio>\n#include <string>')
        with tempfile.TemporaryDirectory(prefix="bw16-ui-ready-") as tmp:
            cpp = Path(tmp) / "correlation.cpp"
            exe = Path(tmp) / "correlation.exe"
            cpp.write_text(source, encoding="utf-8")
            build = subprocess.run(
                [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "firmware/stm32/Core/Inc"), str(cpp), "-o", str(exe)],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("BW16 UI ready correlation checks passed", run.stdout)

    def test_ack_uses_the_existing_generic_path_without_changing_it(self):
        diff = subprocess.run(
            ["git", "diff", "--quiet", "HEAD", "--", str(BUS.relative_to(ROOT))],
            cwd=ROOT,
        )
        self.assertEqual(diff.returncode, 0, "event_bus.h must stay unchanged")
        services = SERVICES.read_text(encoding="utf-8")
        self.assertIn("CrabitLink::subscribe(Topics::SysAck", services)
        self.assertIn("return Crabit::Bw16Ui::acceptAck(request_id, value);", services)
        bus = BUS.read_text(encoding="utf-8")
        # The unrelated protocol still auto-ACKs correlated messages; the UI
        # observer only consumes its own id and runs before that final step.
        self.assertIn('publish("ack", ack_val, message.request_id);', bus)

    def test_stm32_waits_for_claim_and_handles_failures(self):
        text = APP.read_text(encoding="utf-8")
        self.assertIn("const uint16_t request_id = ui ? Crabit::Bw16Ui::begin() : 0;", text)
        self.assertIn("UiReadyTimeoutMs = 3000", text)
        self.assertIn("checkEscape(now);", text)
        self.assertIn('ui_fail_label = "UI TIMEOUT";', text)
        self.assertIn('ui_fail_label = "UART SEND FAILED";', text)
        self.assertIn("if (Display::finishClaim())", text)
        self.assertNotIn("Display::finishClaim();", text)
        # A failed claim must not clear the handover latch or show recovery.
        restored = text.index("case State::DisplayWakeWait:")
        failed = text.index("case State::RestoreFailed:")
        self.assertIn("g_coproc_active = false;", text[restored:failed])
        self.assertIn("requestExit();", text[failed:])
        self.assertIn("Crabit::AmebaPhase::Transition", text[failed:])
        # No UI handover may be treated as ready before a correlated answer.
        mode_wait = text.index("case State::ModeWait:")
        ready_wait = text.index("case State::UiReadyWait:")
        self.assertIn("Crabit::Bw16Ui::Status::Ready", text[mode_wait:ready_wait])
        self.assertIn("state = State::PowerOff;", text[mode_wait:ready_wait])

    def test_bw16_answers_only_after_claim_and_cleans_before_release(self):
        services = BW16_SERVICES.read_text(encoding="utf-8")
        self.assertIn('return enqueue(2, message.value, message.request_id);', services)
        self.assertIn('strcmp(message.value, "UI") != 0', services)
        self.assertIn('CrabitLink::send(Topics::SysAck, ready ? "UI_READY" : "UI_FAILED", request_id);', services)
        self.assertIn("if (request_id)", services)
        # Ordering for UI exit: apps, bus, then the answer.
        cleanup = services.index("AppManager::shutdown();")
        release = services.index("releaseOledBus();", cleanup)
        notify = services.index('CrabitLink::send(Topics::SysAck, "UI_EXIT", request_id);', release)
        self.assertLess(cleanup, release)
        self.assertLess(release, notify)
        self.assertLess(services.index('CrabitLink::send(Topics::SysNotify, "BW16_EXIT");', notify),
                        services.index("current_mode = MODE_SLEEP;", notify))
        entry = BW16_ENTRY.read_text(encoding="utf-8")
        start = entry[entry.index("bool start_ui_mode()"):]
        self.assertLess(start.index("canvas.initHardware();"), start.index("AppManager::launch"))
        self.assertIn("AppManager::init(&canvas, &input);", start)
        self.assertIn("return is_display_active;", start)
        worker = entry[entry.index("void Task_UART_Worker"):]
        self.assertIn('strcmp(message.value, "UI") == 0', worker)

    def test_startup_keeps_early_blanking(self):
        text = STARTUP.read_text(encoding="utf-8")
        pulse = text.index("HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);")
        init = text.index("Display::init();", pulse)
        blank = text.index("Display::fill(0x00);", init)
        loop = text.index("NanoUI_MainLoop();", blank)
        self.assertLess(pulse, init)
        self.assertLess(init, blank)
        self.assertLess(blank, loop)
        self.assertEqual(text.count("Display::init();"), 1)


if __name__ == "__main__":
    unittest.main()
