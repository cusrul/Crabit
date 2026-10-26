"""Host tests execute CyberLinkApp; port-loop integration checks are source guards."""

import shutil
import subprocess
import tempfile
import unittest
import re
from pathlib import Path
from tools.tests.test_bw16_uart_ingress import extract_function

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "firmware/stm32/Core/NanoUI/app_real/app_cyber_link.h"
COPROC = ROOT / "firmware/stm32/Core/NanoUI/app_real/app_coproc.h"
SESSION = ROOT / "firmware/stm32/Core/Inc/ameba_session.h"
PORT = ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp"
FAKES = ROOT / "tools/tests/ameba_frame_fakes"
BASE = "f2b707f4bb523f17cae7b8e771eb1700e7b16056"


class STM32AmebaFrameTests(unittest.TestCase):
    def compiler(self):
        compiler = next(
            (
                p
                for p in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++"))
                if p and Path(p).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "an existing local g++ is required")
        return compiler

    def test_entire_protected_coprocessor_header_matches_base(self):
        original = subprocess.run(
            ["git", "show", f"{BASE}:firmware/stm32/Core/NanoUI/app_real/app_coproc.h"],
            cwd=ROOT,
            capture_output=True,
            check=True,
        ).stdout
        current = COPROC.read_bytes().replace(b"\r\n", b"\n")
        baseline = original.replace(b"\r\n", b"\n")
        self.assertEqual(current, baseline, "protected app_coproc.h changed from approved base")

    def test_session_interface_is_atomic_and_has_explicit_phases(self):
        text = SESSION.read_text(encoding="utf-8")
        for marker in (
            "std::atomic<AmebaPhase> ameba_phase",
            "Inactive, Transition, Running",
            "memory_order_release",
            "memory_order_acquire",
        ):
            self.assertIn(marker, text)

    def test_network_mode_preserves_proven_hardware_calls_and_waits(self):
        original = (
            subprocess.run(
                ["git", "show", "ce7a85c:firmware/stm32/Core/NanoUI/app_real/app_cyber_link.h"],
                cwd=ROOT,
                capture_output=True,
                check=True,
            )
            .stdout.decode("utf-8")
            .replace("\r\n", "\n")
        )
        current = APP.read_text(encoding="utf-8")
        new = 'EventBus::tryPublish("sys/mode", "BLE", Crabit::CompanionBle::sessionId())'
        self.assertEqual(current.count(new), 1)
        # WIFI adds a service branch; preserve the actual hardware operations,
        # their ordering/arguments and every existing frame-time gate.
        hardware = r"\b(?:BW16_NormalBoot|BW16_ForceOff|STM32_PrepareAmebaUartRx|Safe_SPI_Release|Safe_SPI_Claim|HAL_SPI_DeInit|HAL_SPI_Init|OLED_Init|OLED_SetSleep)\([^;]*?\)"
        self.assertEqual(re.findall(hardware, current), re.findall(hardware, original))
        waits = r"uint32_t\(now - (?:state_start|anim_timer)\)\s*[<>]=?\s*\d+"
        # The new pre-entry pause has its own bounded owner-release deadline;
        # it must not change any gate in the existing boot/restore sequence.
        current_waits = re.findall(waits, current)
        self.assertEqual(current_waits[0], "uint32_t(now - state_start) >= 6000")
        self.assertEqual(current_waits[1:], re.findall(waits, original))
        for signature in ("void checkEscape(uint32_t now)", "void runLink(uint32_t now)"):
            expected = extract_function(original, signature)
            if signature == "void checkEscape(uint32_t now)":
                # User requested a silent escape; all hardware/timing stays unchanged.
                expected = expected.replace("                SysAudio::play(4000, 200);\n", "")
                self.assertNotIn("SysAudio::play", extract_function(current, signature))
            self.assertEqual(extract_function(current, signature), expected)

    def test_mount_resets_optical_timestamp_dedup_state(self):
        text = APP.read_text(encoding="utf-8")
        mount_start = text.index("void onMount(Canvas* target, InputDriver* source) override {")
        update_start = text.index("void onUpdate(uint32_t)", mount_start)
        mount = text[mount_start:update_start]
        self.assertIn("last_optical_sample = 0;", mount)
        self.assertIn("optical_sample_seen = false;", mount)

    def test_port_keeps_frame_session_scheduling_and_recovery_gates(self):
        text = PORT.read_text(encoding="utf-8")
        self.assertIn("SysPower::tick()", text)
        self.assertIn(
            "if (!Crabit::amebaSessionActive()) SysPower::tick();",
            text,
        )
        self.assertIn("if (!Crabit::amebaTransitionActive())", text)
        self.assertIn(
            "EventBus::processPending((Crabit::amebaSessionActive() || Crabit::CompanionBle::active())",
            text,
        )
        self.assertIn("const bool allow_recovery = !Crabit::amebaSessionActive() &&", text)
        self.assertIn("!Crabit::CompanionBle::hardwareActive();", text)
        self.assertIn("SensorHub", text)
        lines = text.splitlines()
        lock_branch = next(i for i, line in enumerate(lines) if line.strip() == "if (i2c_locked) {")
        transition_gate = next(
            i for i, line in enumerate(lines) if "if (!Crabit::amebaTransitionActive())" in line
        )
        budgeted_drain = next(
            i
            for i, line in enumerate(lines)
            if "EventBus::processPending((Crabit::amebaSessionActive() || Crabit::CompanionBle::active())"
            in line
        )
        indent = lambda i: len(lines[i]) - len(lines[i].lstrip())
        self.assertGreater(indent(transition_gate), indent(lock_branch))
        self.assertGreater(indent(budgeted_drain), indent(transition_gate))
        self.assertLess(lock_branch, transition_gate)
        self.assertLess(transition_gate, budgeted_drain)

    def test_frame_exit_does_not_double_poll_or_implicitly_draw_root(self):
        text = PORT.read_text(encoding="utf-8")
        owned = text.index("const bool frame_owned_input = AppManager::frameAppActive();")
        tick = text.index("AppManager::tick();", owned)
        root_poll_guard = text.index(
            "if (!frame_owned_input && !AppManager::frameAppActive()) wm.update();", tick
        )
        root_draw_guard = text.index(
            "if (!g_coproc_active && !AppManager::frameAppActive())", root_poll_guard
        )
        self.assertLess(owned, tick)
        self.assertLess(tick, root_poll_guard)
        self.assertLess(root_poll_guard, root_draw_guard)
        self.assertIn("AppManager::frameAppActive()", text[root_draw_guard : root_draw_guard + 180])

    def test_event_queue_budget_is_optional_and_phase_specific(self):
        bus = (ROOT / "firmware/stm32/Core/Inc/event_bus.h").read_text(encoding="utf-8")
        main = PORT.read_text(encoding="utf-8")
        self.assertIn("processPending(size_t max_messages = static_cast<size_t>(-1))", bus)
        self.assertIn("processed < max_messages", bus)
        self.assertIn("if (!Crabit::amebaTransitionActive())", main)
        self.assertIn(
            "EventBus::processPending((Crabit::amebaSessionActive() || Crabit::CompanionBle::active())",
            main,
        )
        self.assertIn("? 3 : static_cast<size_t>(-1));", main)

    def test_real_frame_header_runs_bounded_host_scenarios(self):
        self.assertTrue(APP.is_file(), "new app header is not available")
        with tempfile.TemporaryDirectory(prefix="stm32-ameba-frame-") as tmp:
            temp = Path(tmp)
            # The copied source bytes are the production header; its quoted local
            # app_coproc include is replaced only in this isolated compile fixture.
            copied = temp / "app_cyber_link.h"
            shutil.copyfile(APP, copied)
            self.assertEqual(copied.read_bytes(), APP.read_bytes())
            app_source = copied.read_text(encoding="utf-8")
            self.assertIn("AppMode getMode() override { return APP_MODE_FRAME; }", app_source)
            self.assertNotIn("AppManager::exitCurrentApp", app_source)
            self.assertNotIn("while (running)", app_source)
            shutil.copyfile(FAKES / "app_coproc.h", temp / "app_coproc.h")
            binary = temp / "ameba-frame-host.exe"
            command = [
                self.compiler(),
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-I",
                str(FAKES),
                "-I",
                str(ROOT / "firmware/stm32/Core/Inc"),
                "-I",
                str(temp),
                str(ROOT / "tools/tests/stm32_ameba_frame.cpp"),
                "-o",
                str(binary),
            ]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("STM32 AMEBA frame host checks passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
