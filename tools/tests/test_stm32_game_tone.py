"""Host checks for the strict app/tone contract and UART->driver handoff.

The service module is deliberately only protocol validation plus one atomic
latest-value word. Scheduling lives in the existing Buzzer_Driver and is covered
by test_stm32_game_tone_driver.py.
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
FIXTURE = Path(__file__).resolve().parent / "stm32_game_tone_host.cpp"
SERVICE = AUDIO / "game_tone_service.cpp"
CONTRACT = SHARED / "game_tone.h"
TOPICS = SHARED / "topics.h"
UART = STM32 / "Core/Src/uart_task.cpp"
HW_SENSORS = STM32 / "Core/Inc/hw_sensors.h"
CMAKE = STM32 / "CMakeLists.txt"


def compiler():
    for candidate in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++")):
        if candidate and Path(candidate).is_file():
            return candidate
    return None


class GameToneContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cxx = compiler()
        if not cls.cxx:
            raise unittest.SkipTest("an existing local g++ is required")
        cls.temp = tempfile.TemporaryDirectory(prefix="crabit-game-tone-")
        cls.exe = Path(cls.temp.name) / "game-tone-host.exe"
        command = [
            cls.cxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-pthread",
            "-I", str(Path(__file__).resolve().parent / "game_tone_host_rtos"),
            "-I", str(SHARED), "-I", str(AUDIO),
            str(FIXTURE), str(SERVICE), "-o", str(cls.exe),
        ]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if result.returncode:
            raise AssertionError("game-tone host fixture compile failed\n"
                                 + result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_contract_parser_and_handoff(self):
        result = subprocess.run([str(self.exe)], cwd=ROOT, text=True,
                                capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("STM32 game-tone host checks passed", result.stdout)

    def test_contract_is_cxx11_safe_for_bw16(self):
        # BW16 currently builds with the older ARM g++/gnu++11; keep the shared
        # contract usable on both ends rather than introducing C++17 variables.
        with tempfile.TemporaryDirectory(prefix="crabit-game-tone-cxx11-") as tmp:
            source = Path(tmp) / "contract.cpp"
            source.write_text(
                '#include "game_tone.h"\n'
                "int main() {\n"
                "  CrabitTone::Tone tone;\n"
                "  return CrabitTone::parse(\"440,30\", 6, &tone) ==\n"
                "         CrabitTone::ParseResult::Cue &&\n"
                "         CrabitTone::TOPIC == Topics::AppTone ? 0 : 1;\n"
                "}\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [self.cxx, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                 "-fsyntax-only", "-I", str(SHARED), str(source)],
                cwd=ROOT, text=True, capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_topic_and_single_driver_ownership(self):
        self.assertIn('static constexpr const char* AppTone = "app/tone";',
                      TOPICS.read_text(encoding="utf-8"))
        contract = CONTRACT.read_text(encoding="utf-8")
        self.assertIn("Topics::AppTone", contract)
        self.assertIn("kLeaseMs = 250", contract)
        self.assertIn("kMaxStartsPerSecond = 20", contract)

        uart = UART.read_text(encoding="utf-8")
        self.assertIn("CrabitTone::handleFrame(", uart)
        self.assertLess(uart.index("Topics::AppTone"), uart.index("LinkQueue::enqueueUi"))
        for forbidden in ("HAL_TIM_", "__HAL_TIM", "GlobalStore.req_buzzer"):
            self.assertNotIn(forbidden, uart)
        # No second receive queue/task for tones.
        for forbidden in ("xQueueCreate", "xTaskCreate", "GameToneTask"):
            self.assertNotIn(forbidden, uart[uart.index("Task_Modem"):])

        hw = HW_SENSORS.read_text(encoding="utf-8")
        start = hw.index("class Buzzer_Driver")
        buzzer = hw[start:]
        for required in (
            "CrabitTone::take(now)",
            "CrabitTone::discardPending()",
            "CrabitTone::kStopWord",
            "source == ToneSource::Game",
            "CrabitPreferences::enabled(CrabitPreferences::Sound)",
            "GlobalStore.sys_volume == 0",
            "Crabit::amebaSessionActive() || g_coproc_active",
            "static_cast<int32_t>(now - beep_end_time)",
            "Sensor(10)",
        ):
            self.assertIn(required, buzzer)
        self.assertNotIn("SysPower::", buzzer)

        service = SERVICE.read_text(encoding="utf-8")
        for forbidden in (" new ", "malloc(", "xQueue", "xTaskCreate", "HAL_TIM"):
            self.assertNotIn(forbidden, service)
        self.assertIn("taskENTER_CRITICAL()", service)
        self.assertIn("taskEXIT_CRITICAL()", service)
        publish = service[service.index("bool handleFrame"):service.index("uint32_t take")]
        self.assertLess(publish.index("CriticalSection guard"), publish.index("pending_tick_.store"))
        self.assertLess(publish.index("pending_tick_.store"), publish.index("pending_word_.store"))
        consume = service[service.index("uint32_t take"):service.index("void discardPending")]
        self.assertLess(consume.index("CriticalSection guard"), consume.index("pending_word_.exchange"))
        self.assertLess(consume.index("pending_word_.exchange"), consume.index("pending_tick_.load"))
        self.assertLess(consume.index("word == kStopWord"), consume.index("now_ms - posted"))

        cmake = CMAKE.read_text(encoding="utf-8")
        self.assertEqual(cmake.count("product/services/audio/game_tone_service.cpp"), 1)
        self.assertIn("${CMAKE_CURRENT_SOURCE_DIR}/product/services/audio", cmake)


if __name__ == "__main__":
    unittest.main()
