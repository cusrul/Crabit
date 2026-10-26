"""Bounded checks for the real SysPower tick diagnostic guard."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
POWER_HEADER = ROOT / "firmware/stm32/Core/Inc/sys_power.h"
CMAKE_FILE = ROOT / "firmware/stm32/CMakeLists.txt"
GUARD = """        if (CRABIT_DEBUG_STAY_AWAKE) { feed(); return; }
"""


class DebugAwakeTests(unittest.TestCase):
    def test_default_off_and_original_tick_body_retained(self):
        source = POWER_HEADER.read_text(encoding="utf-8")
        self.assertIn(
            "#ifndef CRABIT_DEBUG_STAY_AWAKE\n#define CRABIT_DEBUG_STAY_AWAKE 0\n#endif", source
        )
        tick = source.split("    static void tick() {", 1)[1].split("\n    }\n};", 1)[0]
        guard_at = tick.index(GUARD)
        original = tick[guard_at + len(GUARD) :]
        self.assertTrue(
            original.startswith("        uint32_t now = HAL_GetTick();"), original[:100]
        )
        for retained in (
            "HAL_GPIO_ReadPin",
            "STM32_ReadSensorSnapshot",
            "OLED_SetSleep(true)",
            "prepareDeepSleep();",
            "enterDeepSleep();",
            "restoreFromScreenOff();",
        ):
            self.assertIn(retained, original)
        cmake = CMAKE_FILE.read_text(encoding="utf-8")
        self.assertRegex(cmake, r"option\(CRABIT_DEBUG_STAY_AWAKE[^\n]* OFF\)")
        self.assertRegex(cmake, r"option\(CRABIT_DIAG_SKIP_ATTACH[^\n]* OFF\)")

    def test_on_guard_executes_actual_extracted_prefix_and_returns(self):
        source = POWER_HEADER.read_text(encoding="utf-8")
        tick = source.split("    static void tick() {", 1)[1].split("\n    }\n};", 1)[0]
        guard_at = tick.index(GUARD)
        extracted_prefix = tick[guard_at : guard_at + len(GUARD)]
        compiler = next(
            (
                p
                for p in (r"C:\\w64dev\\w64devkit\\bin\\g++.exe", shutil.which("g++"))
                if p and Path(p).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "an existing local g++ is required")
        # The fixture executes only the exact production guard prefix. The rest
        # of SysPower::tick depends on MCU HAL/application types and is checked
        # above for retention/order, not simulated as a full firmware runtime.
        fixture = (
            """#include <cassert>
static int feeds = 0, body = 0;
static const int CRABIT_DEBUG_STAY_AWAKE = 1;
static void feed() { ++feeds; }
static void tick() {
"""
            + extracted_prefix
            + "        ++body; // stand-in for the first original-body statement\n}\nint main() { tick(); assert(feeds == 1 && body == 0); }\n"
        )
        with tempfile.TemporaryDirectory(prefix="crabit-debug-awake-") as tmp:
            cpp = Path(tmp) / "guard.cpp"
            binary = Path(tmp) / "guard.exe"
            cpp.write_text(fixture, encoding="utf-8")
            build = subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(cpp),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
