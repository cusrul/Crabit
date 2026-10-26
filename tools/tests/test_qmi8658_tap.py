"""Compile and run host tests against the exact QMI8658 class in hw_sensors.h."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
DRIVER_HEADER = ROOT / "firmware/stm32/Core/Inc/hw_sensors.h"
FIRMWARE_INCLUDE = ROOT / "firmware/stm32/Core/Inc"
CPP_FIXTURE = ROOT / "tools/tests/qmi8658_tap.cpp"
INCLUDE_DIRECTIVE = re.compile(rb"^[ \t]*#include[ \t]+[<\"][^>\"]+[>\"][ \t]*(?:\r?\n)?$")
CLASS_START = b"class QMI8658_Driver : public NanoUI::Sensor {"
BATTERY_MARKER = "// 2. 电量".encode("utf-8")


def stage_production_driver(temp_dir: Path) -> None:
    """Keep every production byte up to the next driver, except include lines."""
    source = DRIVER_HEADER.read_bytes()
    if source.count(CLASS_START) != 1:
        raise AssertionError("expected one QMI8658_Driver class start in hw_sensors.h")
    if source.count(BATTERY_MARKER) != 1:
        raise AssertionError("expected one // 2. 电量 boundary in hw_sensors.h")

    start = source.index(CLASS_START)
    boundary = source.index(BATTERY_MARKER)
    if start >= boundary:
        raise AssertionError("QMI8658_Driver class start must precede the battery boundary")
    if b"};" not in source[start:boundary]:
        raise AssertionError("battery boundary occurs before the QMI8658_Driver class ends")

    bounded_source = source[:boundary]
    lines = bounded_source.splitlines(keepends=True)
    retained_lines = [line for line in lines if not INCLUDE_DIRECTIVE.fullmatch(line)]
    include_count = len(lines) - len(retained_lines)
    if include_count == 0:
        raise AssertionError("expected production include lines to remove for host staging")

    staged_body = b"".join(retained_lines)
    class_end = source.rfind(b"};", start, boundary)
    if class_end < start:
        raise AssertionError("QMI8658_Driver class has no closing brace before the boundary")
    staged_start = staged_body.index(CLASS_START)
    staged_end = staged_body.rfind(b"};", staged_start) + 2
    if source[start : class_end + 2] != staged_body[staged_start:staged_end]:
        raise AssertionError("staging changed production QMI8658_Driver bytes")

    staged = staged_body + b"\n#endif  // HW_SENSORS_H (host staging wrapper)\n"

    (temp_dir / "qmi8658_tap_staged.hpp").write_bytes(staged)


class Qmi8658TapHostTests(unittest.TestCase):
    def test_exact_production_driver(self):
        compiler_candidates = (
            r"C:\w64dev\w64devkit\bin\g++.exe",
            shutil.which("g++"),
        )
        compiler = next(
            (
                candidate
                for candidate in compiler_candidates
                if candidate and Path(candidate).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "an existing host g++ compiler is required")

        with tempfile.TemporaryDirectory(prefix="crabit-qmi8658-tap-") as directory:
            temp_dir = Path(directory)
            stage_production_driver(temp_dir)
            binary = temp_dir / "qmi8658_tap.exe"
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
                    str(FIRMWARE_INCLUDE),
                    str(CPP_FIXTURE),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)

            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("QMI8658 production tap tests PASS", run.stdout)


if __name__ == "__main__":
    unittest.main()
