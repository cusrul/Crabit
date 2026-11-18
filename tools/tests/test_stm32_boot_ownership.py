"""Check the STM32 boot owners and exercise real Fault code with a fake RTC."""

from pathlib import Path
import re
import shutil
import subprocess

import pytest


STM32 = Path(__file__).resolve().parents[2] / "firmware/stm32"


def test_board_registers_each_sensor_once_and_logs_final_qmi_configuration():
    source = (STM32 / "product/board/board.cpp").read_text(encoding="utf-8")
    board = source.split("void Board::init() {", 1)[1].split("void Board::startTasks()", 1)[0]
    drivers = ["imu_drv", "batt_drv", "pat_drv", "buzzer_drv", "bmp_drv", "axp_drv"]
    assert re.findall(r"\bregisterSensor\(\s*&(\w+)\s*\)", board) == drivers
    assert not re.search(r"\b(?:" + "|".join(drivers) + r")\s*\.\s*init\s*\(", board)
    assert board.index("registerSensor(&imu_drv)") < board.index("imu_drv.tapReady()")


def test_cold_boot_blanks_once_before_board_applies_display_preferences():
    startup = (STM32 / "Core/Src/startup.cpp").read_text(encoding="utf-8")
    board = (STM32 / "product/board/board.cpp").read_text(encoding="utf-8")
    assert startup.count("Display::init();") == 1
    assert startup.index("Display::init();") < startup.index("Display::fill(0x00)") < startup.index("NanoUI_MainLoop();")
    assert "Display::init();" not in board
    assert "sys_cfg.brightness" in board and "Display::setInvert(sys_cfg.invert_disp)" in board


def test_fault_register_is_read_once_and_preserves_rtc_marker(tmp_path):
    compiler = shutil.which("g++")
    if not compiler:
        pytest.skip("g++ is required for the RTC register fake")
    for header in ("FreeRTOS.h", "task.h"):
        (tmp_path / header).write_text("", encoding="utf-8")
    (tmp_path / "main.h").write_text(
        """
#pragma once
#include <stdint.h>
struct FakeRtc { volatile uint32_t BKP0R, BKP1R; };
extern FakeRtc fakeRtc;
#define RTC (&fakeRtc)
struct SimulatedReset {};
inline void __HAL_RCC_PWR_CLK_ENABLE() {}
inline void __HAL_RCC_RTCAPB_CLK_ENABLE() {}
inline void HAL_PWR_EnableBkUpAccess() {}
inline void HAL_PWR_DisableBkUpAccess() {}
inline void __disable_irq() {}
[[noreturn]] inline void NVIC_SystemReset() { throw SimulatedReset{}; }
inline uint32_t HAL_GetTick() { return 0; }
""", encoding="utf-8")
    fixture = tmp_path / "fixture.cpp"
    fixture.write_text(
        """
#include <cassert>
#include "health.h"
#include "main.h"
FakeRtc fakeRtc{0x32F2u, 0};
int main() {
    assert(Fault::lastBoot() == Fault::Reason::None);
    try {
        Fault::reset(Fault::Reason::Uart);
    } catch (const SimulatedReset&) {
        assert(fakeRtc.BKP0R == 0x32F2u && fakeRtc.BKP1R != 0);
    }
    assert(Fault::lastBoot() == Fault::Reason::Uart);
    assert(Fault::lastBoot() == Fault::Reason::None);
    assert(fakeRtc.BKP0R == 0x32F2u && fakeRtc.BKP1R == 0);
}
""", encoding="utf-8")
    rtc = (STM32 / "Core/Src/rtc.c").read_text(encoding="utf-8")
    assert "HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == 0x32F2" in rtc
    binary = tmp_path / "fault-test.exe"
    subprocess.run([
        compiler, "-std=c++17", "-I", str(tmp_path), "-I", str(STM32 / "Core/Inc"),
        str(STM32 / "Core/Src/health.cpp"), str(fixture), "-o", str(binary),
    ], check=True, capture_output=True, text=True)
    subprocess.run([str(binary)], check=True, capture_output=True, text=True)
