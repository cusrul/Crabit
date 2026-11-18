"""Run the exact Buzzer_Driver class from hw_sensors.h on the host.

The class is staged byte-for-byte with only its include lines replaced; the real
GlobalStore, SysAudio, preferences, session predicate and game-tone handoff are
compiled. Fake timer registers and HAL tick observe the production 10 ms path.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STM32 = ROOT / "firmware/stm32"
SHARED = ROOT / "firmware/shared"
AUDIO = STM32 / "product/services/audio"
HW_SENSORS = STM32 / "Core/Inc/hw_sensors.h"
FIXTURE = Path(__file__).resolve().parent / "stm32_game_tone_driver_host.cpp"
FAKES = Path(__file__).resolve().parent / "game_tone_driver_fakes.h"
SERVICE = AUDIO / "game_tone_service.cpp"
CLASS_START = "class Buzzer_Driver : public NanoUI::Sensor {"


def compiler():
    for candidate in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++")):
        if candidate and Path(candidate).is_file():
            return candidate
    return None


def extract_driver() -> str:
    text = HW_SENSORS.read_text(encoding="utf-8")
    if text.count(CLASS_START) != 1:
        raise AssertionError("expected exactly one Buzzer_Driver class")
    start = text.index(CLASS_START)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1] + ";\n"
    raise AssertionError("unterminated Buzzer_Driver class")


class GameToneDriverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cxx = compiler()
        if not cls.cxx:
            raise unittest.SkipTest("an existing local g++ is required")
        cls.temp = tempfile.TemporaryDirectory(prefix="crabit-game-tone-driver-")
        temp = Path(cls.temp.name)
        staged = temp / "buzzer_driver_staged.hpp"
        staged.write_text(extract_driver(), encoding="utf-8")
        cls.exe = temp / "game-tone-driver.exe"
        command = [
            cls.cxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-pthread",
            "-I", str(temp),
            "-I", str(Path(__file__).resolve().parent),
            "-I", str(Path(__file__).resolve().parent / "game_tone_host_rtos"),
            "-I", str(SHARED),
            "-I", str(AUDIO),
            "-I", str(STM32 / "Core/Inc"),
            "-I", str(STM32 / "Core/NanoUI/include"),
            "-I", str(ROOT / "libraries/NanoUI/src"),
            str(FIXTURE), str(SERVICE), "-o", str(cls.exe),
        ]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if result.returncode:
            raise AssertionError("driver host fixture compile failed\n"
                                 + result.stdout + result.stderr)
        cls.command = command

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_exact_driver_ownership_and_timing(self):
        result = subprocess.run([str(self.exe)], cwd=ROOT, text=True,
                                capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("STM32 Buzzer_Driver game-tone checks passed", result.stdout)

    def test_staged_class_is_the_real_production_text(self):
        staged = extract_driver()
        header = HW_SENSORS.read_text(encoding="utf-8")
        self.assertTrue(staged.endswith("};\n"))
        self.assertIn(staged[:-2], header)
        for forbidden in ("#include", "#define", "new ", "malloc"):
            self.assertNotIn(forbidden, staged)
        # The class still has no private second queue/task.
        self.assertNotRegex(staged, r"xQueue|xTaskCreate|std::thread")


if __name__ == "__main__":
    unittest.main()
