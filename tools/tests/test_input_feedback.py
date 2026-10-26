"""Exercise the entire production InputFeedback header with host-only doubles."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

FAKE_DEPENDENCIES = r"""
#ifndef HW_WS2812_H
#define HW_WS2812_H
#include <cstdint>
static constexpr int LED_COUNT = 2;
class WS2812_Driver {
public:
    struct Pixel { uint8_t r = 0, g = 0, b = 0; };
    Pixel pixels[LED_COUNT]{};
    uint8_t brightness = 0;
    unsigned show_calls = 0;
    unsigned clear_calls = 0;
    void clear() {
        for (auto& pixel : pixels) pixel = Pixel{};
        brightness = 0;
        ++clear_calls;
    }
    void setBrightness(uint8_t value) { brightness = value; }
    void setLED(int index, uint8_t r, uint8_t g, uint8_t b) {
        if (index >= 0 && index < LED_COUNT) pixels[index] = {r, g, b};
    }
    void show() { ++show_calls; }
};
extern WS2812_Driver led_drv;
#endif

#ifndef __MAIN_H
#define __MAIN_H
#include <cstdint>
uint32_t HAL_GetTick();
#endif
"""


class InputFeedbackTests(unittest.TestCase):
    def test_expiry_tick_precedes_screen_active_gate_in_ui_loop(self):
        source = (ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp").read_text(encoding="utf-8")
        loop_start = source.index(
            "while(1) {", source.index('extern "C" void NanoUI_MainLoop(void)')
        )
        loop = source[loop_start:]
        diag_tick = loop.index("CrabitUsb_DiagTick();")
        feedback_tick = loop.index("InputFeedback::tick();")
        active_gate = loop.index("if (SysPower::isActive())")

        self.assertLess(diag_tick, feedback_tick)
        self.assertLess(feedback_tick, active_gate)
        self.assertEqual(loop.count("InputFeedback::tick();"), 1)

    def test_whole_production_header_with_host_hardware_doubles(self):
        compiler = shutil.which("g++") or r"C:\w64dev\w64devkit\bin\g++.exe"
        self.assertTrue(Path(compiler).is_file(), "an existing host g++ is required")

        with tempfile.TemporaryDirectory(prefix="crabit-input-feedback-") as folder:
            fake_header = Path(folder) / "fake_hw_and_main.h"
            binary = Path(folder) / "input_feedback.exe"
            fake_header.write_text(FAKE_DEPENDENCIES, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-include",
                    str(fake_header),
                    str(ROOT / "tools/tests/input_feedback.cpp"),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
