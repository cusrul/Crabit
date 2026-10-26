"""Compile and run host scenarios against the complete production SysPower class."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
POWER_HEADER = ROOT / "firmware/stm32/Core/Inc/sys_power.h"
FAKE_HEADER = ROOT / "tools/tests/power_fakes/platform.hpp"
CPP_FIXTURE = ROOT / "tools/tests/screen_power.cpp"
INCLUDE_DIRECTIVE = re.compile(rb'^[ \t]*#include[ \t]+[<"][^>"]+[>"][ \t]*(?:\r?\n)?$')


def stage_platform_header(temp_dir: Path) -> None:
    """Stage the full production header, redirecting include lines only."""
    original = POWER_HEADER.read_bytes()
    lines = original.splitlines(keepends=True)
    staged_lines = []
    include_count = 0
    for line in lines:
        if INCLUDE_DIRECTIVE.fullmatch(line):
            include_count += 1
            ending = b"\r\n" if line.endswith(b"\r\n") else b"\n" if line.endswith(b"\n") else b""
            prefix = line[: len(line) - len(line.lstrip(b" \t"))]
            staged_lines.append(prefix + b'#include "platform.hpp"' + ending)
        else:
            staged_lines.append(line)

    staged = b"".join(staged_lines)
    original_non_includes = [line for line in lines if not INCLUDE_DIRECTIVE.fullmatch(line)]
    staged_non_includes = [line for line in staged_lines if not INCLUDE_DIRECTIVE.fullmatch(line)]
    if include_count == 0:
        raise AssertionError("production SysPower header has no replaceable platform includes")
    if staged_non_includes != original_non_includes:
        raise AssertionError(
            "staging changed production SysPower content beyond include directives"
        )

    (temp_dir / "sys_power.h").write_bytes(staged)
    (temp_dir / "platform.hpp").write_bytes(FAKE_HEADER.read_bytes())


class ScreenPowerHostTests(unittest.TestCase):
    def test_retained_supply_stop2_policy_runtime(self):
        compiler = shutil.which("g++") or r"C:\w64dev\w64devkit\bin\g++.exe"
        self.assertTrue(Path(compiler).is_file())
        with tempfile.TemporaryDirectory(prefix="crabit-stop2-restore-") as temp:
            temp_dir = Path(temp)
            stage_platform_header(temp_dir)
            binary = temp_dir / "stop2_restore.exe"
            build = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    "-DCRABIT_POWER_SCREEN_ONLY=0",
                    "-I",
                    str(temp_dir),
                    "-I",
                    str(ROOT / "firmware/stm32/Core/Inc"),
                    str(ROOT / "tools/tests/retained_power.cpp"),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("4 passed", run.stdout)

    def test_screen_off_sensor_worker_has_bounded_qmi_only_polling(self):
        # Source guard, not execution of the FreeRTOS worker or I2C interrupts.
        port = (ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp").read_text(encoding="utf-8")
        marker = "} else if (SysPower::canPollGesture() && STM32_I2C_Lock(20)) {"
        self.assertEqual(port.count(marker), 1)
        branch = port.split(marker, 1)[1].split("vTaskDelay(pdMS_TO_TICKS(5));", 1)[0]
        for gate in (
            "SysPower::canPollGesture()",
            "!Crabit::amebaSessionActive()",
            "!Crabit::CompanionBle::hardwareTransitioning()",
            "HAL_I2C_GetState(&hi2c1) == HAL_I2C_STATE_READY",
            "imu_drv.checkTick(now)",
            "imu_drv.update();",
            "PublishSensorSnapshot(now);",
            "STM32_I2C_Unlock();",
        ):
            self.assertIn(gate, branch)
        for forbidden in ("RecoverSensorBus(", "AXP2101_", "SensorManager::", "BW16_"):
            self.assertNotIn(forbidden, branch)

    def test_full_production_sys_power_runtime(self):
        compiler = next(
            (
                candidate
                for candidate in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++"))
                if candidate and Path(candidate).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "an existing host g++ compiler is required")

        with tempfile.TemporaryDirectory(prefix="crabit-screen-power-") as temp:
            temp_dir = Path(temp)
            stage_platform_header(temp_dir)
            binary = temp_dir / "screen_power.exe"
            build = subprocess.run(
                [
                    str(compiler),
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    "-I",
                    str(temp_dir),
                    "-I",
                    str(ROOT / "firmware/stm32/Core/Inc"),
                    str(CPP_FIXTURE),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)

            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("18 passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
