"""Run the real STM32 companion BLE implementation against narrow host doubles."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/stm32/product/features/companion/companion_ble.cpp"
HARNESS = ROOT / "tools/tests/stm32_ble_background.cpp"
SHARED = ROOT / "firmware/shared"
STM32_INC = ROOT / "firmware/stm32/Core/Inc"
COMPANION_INC = ROOT / "firmware/stm32/product/features/companion"


class Stm32BleBackgroundTests(unittest.TestCase):
    def test_actual_production_body_and_shared_policy(self):
        compiler = next(
            (
                p
                for p in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++"))
                if p and Path(p).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "an existing local g++ is required")
        source = SOURCE.read_text(encoding="utf-8")
        services = (ROOT / "firmware/stm32/Core/Src/app_services.cpp").read_text(encoding="utf-8")
        self.assertIn("Crabit::CompanionBle::observeModeStatus(payload);", services)
        # C1b boundary: this module now depends only on its injected ports, not
        # on concrete App/UI objects. A leftover direct reference must fail here
        # and would also fail to compile in the harness below, which defines no
        # AppManager/Popup/CyberLinkApp/CompanionPairPrompt at all.
        for forbidden in (
            "app_real/",
            "AppManager::",
            "Popup::",
            "CyberLinkApp",
            "CompanionPairPrompt",
        ):
            self.assertNotIn(forbidden, source)
        for required in (
            "bool bind(const UiHooks& ui, const HardwareHooks& hardware)",
            "ui_hooks.frameAppActive()",
            "ui_hooks.launchBleBackground()",
            "ui_hooks.showToast(",
            "ui_hooks.dismissPairPrompt()",
            "ui_hooks.invalidatePairPrompt(",
            "ui_hooks.tickPairPrompt(",
            "hardware_hooks.normalBoot()",
            "hardware_hooks.forceOff()",
        ):
            self.assertIn(required, source)
        header = (
            ROOT / "firmware/stm32/product/features/companion/companion_ble.h"
        ).read_text(encoding="utf-8")
        for required in (
            "struct UiHooks {",
            "bool (*frameAppActive)();",
            "void (*launchBleBackground)();",
            "void (*showToast)(const char* message);",
            "void (*dismissPairPrompt)();",
            "void (*invalidatePairPrompt)(uint16_t session, uint32_t pair);",
            "void (*tickPairPrompt)(uint16_t session, uint32_t pair);",
            "struct HardwareHooks {",
            "void (*normalBoot)();",
            "void (*forceOff)();",
        ):
            self.assertIn(required, header)
        # The port header stays a plain value-type interface: no UI/HAL include.
        self.assertEqual(
            [line for line in header.splitlines() if line.startswith("#include")],
            ["#include <stdint.h>"],
        )
        # Static source guard: the legacy CMake switch may still be defined,
        # but the production implementation must no longer contain its branch
        # or any ATTACH wire command. The executable fixture below separately
        # proves identical runtime behavior with the macro set to both values.
        self.assertNotIn("#if CRABIT_DIAG_SKIP_ATTACH", source)
        self.assertNotIn('"ATTACH"', source)
        cmake = (ROOT / "firmware/stm32/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('option(CRABIT_DIAG_SKIP_ATTACH "Legacy no-op:', cmake)
        # The moved module keeps the shared policy headers as canonical bare
        # includes resolved by the firmware/stm32/../shared include root; only
        # platform/UI directives are swapped for the narrow host doubles.
        replaced = []
        lines = []
        for line in source.splitlines():
            if line.startswith('#include "'):
                name = line.split('"')[1]
                if name in ("companion_session.h", "companion_probe.h"):
                    lines.append(line)
                else:
                    replaced.append(name)
            else:
                lines.append(line)
        self.assertEqual(
            set(replaced),
            {
                "companion_ble.h",
                "comms_link.h",
                "uart_task.h",
                "topics.h",
                "companion_trace.h",
                "ameba_session.h",
                "sys_power.h",
                "global_store.h",
                "main.h",
            },
        )
        with tempfile.TemporaryDirectory(prefix="crabit-stm32-ble-host-") as tmp:
            tmp = Path(tmp)
            (tmp / "companion_ble_production_fixture.cpp").write_text(
                f'#include "{HARNESS.as_posix()}"\n' + "\n".join(lines) + "\n", encoding="utf-8"
            )
            for skip_attach in (0, 1):
                for probe_only in (0, 1):
                    with self.subTest(skip_attach=skip_attach, probe_only=probe_only):
                        binary = tmp / f"stm32-ble-background-{skip_attach}-{probe_only}.exe"
                        build = subprocess.run(
                            [
                                compiler,
                                "-std=c++11",
                                "-Wall",
                                "-Wextra",
                                "-Werror",
                                f"-DCRABIT_DIAG_SKIP_ATTACH={skip_attach}",
                                f"-DCRABIT_COMPANION_UART_PROBE_ONLY={probe_only}",
                                "-I",
                                str(tmp),
                                "-I",
                                str(SHARED),
                                "-I",
                                str(STM32_INC),
                                "-I",
                                str(COMPANION_INC),
                                str(tmp / "companion_ble_production_fixture.cpp"),
                                "-o",
                                str(binary),
                            ],
                            capture_output=True,
                            text=True,
                        )
                        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                        run = subprocess.run([str(binary)], capture_output=True, text=True)
                        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                        self.assertIn("STM32 BLE background production assertions passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
