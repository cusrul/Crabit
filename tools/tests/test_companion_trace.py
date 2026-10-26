"""Compile the actual bounded trace implementation with CDC/HAL doubles."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CompanionTraceTest(unittest.TestCase):
    def test_actual_trace_buffer_privacy_backpressure_and_flash_exclusion(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="crabit-trace-") as folder:
            directory = Path(folder)
            (directory / "main.h").write_text(
                """#include <stdint.h>
uint32_t HAL_GetTick();
uint32_t __get_PRIMASK();
void __disable_irq();
void __set_PRIMASK(uint32_t);
""",
                encoding="utf-8",
            )
            (directory / "usbd_cdc_if.h").write_text(
                """#include <stdint.h>
#define USBD_OK 0
#define USBD_BUSY 1
uint8_t CDC_Transmit_FS(uint8_t*, uint16_t);
""",
                encoding="utf-8",
            )
            fixture = directory / "fixture.cpp"
            fixture.write_text(
                r"""
#include "companion_trace.h"
#include <cassert>
#include <string>
#include <vector>
volatile uint8_t bw16_flash_mode = 0;
uint32_t now = 1, mask = 0;
bool busy = false;
std::vector<std::string> lines;
uint32_t HAL_GetTick() { return now; }
uint32_t __get_PRIMASK() { return mask; }
void __disable_irq() { mask = 1; }
void __set_PRIMASK(uint32_t value) { mask = value; }
uint8_t CDC_Transmit_FS(uint8_t* bytes, uint16_t size) {
    if (busy) return 1;
    lines.emplace_back(reinterpret_cast<char*>(bytes), size); return 0;
}
int main() {
    using namespace Crabit::CompanionTrace;
    message("sys/comm", "QUEUED", 7); poll();
    assert(lines.size() == 1 && lines.back().find("sid=7 ev=QUEUED") != std::string::npos);
    message("sys/comm", "PAIR_WAIT:PRIVATE_TOKEN", 7); poll();
    assert(lines.back().find("ev=PAIR_WAIT") != std::string::npos);
    message("sys/comm", "PAIR_WAIT:12", 9); poll();
    assert(lines.back().find("sid=9 ev=PAIR_WAIT a=3 b=12") != std::string::npos);
    message("sys/comm", "AUTHENTICATED:12", 9); poll();
    assert(lines.back().find("sid=9 ev=AUTHENTICATED a=3 b=12") != std::string::npos);
    message("sys/comm", "PAIR_CANCELLED:13", 9); poll();
    assert(lines.back().find("sid=9 ev=PAIR_CANCELLED a=3 b=13") != std::string::npos);
    message("sys/comm", "AUTHENTICATED:PRIVATE_TOKEN", 9); poll();
    assert(lines.back().find("ev=MSG_OTHER") != std::string::npos);
    message("diag/radio", "PREFS_BEGIN", 7); poll();
    assert(lines.back().find("ev=BW_PREFS_BEGIN") != std::string::npos);
    message("boot", "uart_ready t=42 mode=0 ui=0", 0); poll();
    assert(lines.back().find("ev=BW_UART_READY") != std::string::npos);
    const Event ui_mode_events[] = {
        UiEntry, UiStage, UiHeartbeat, ModeQueued, ModeTxBegin, ModeTxEnd,
        BleStopRequested, BleOffComplete, BleResumeQueued,
        BwUiInitBegin, BwUiOledReady, BwUiLauncherReady
    };
    const char* const ui_mode_names[] = {
        "UI_ENTRY", "UI_STAGE", "UI_HEARTBEAT", "MODE_QUEUED", "MODE_TX_BEGIN", "MODE_TX_END",
        "BLE_STOP_REQUESTED", "BLE_OFF_COMPLETE", "BLE_RESUME_QUEUED",
        "BW_UI_INIT_BEGIN", "BW_UI_OLED_READY", "BW_UI_LAUNCHER_READY"
    };
    for (unsigned i = 0; i < sizeof(ui_mode_events) / sizeof(ui_mode_events[0]); ++i) {
        const uint16_t session = uint16_t(20 + i);
        const uint32_t a = 100 + i, b = 200 + i;
        record(ui_mode_events[i], session, a, b);
        poll();
        const std::string expected = "CRABIT:RADIO t=1 sid=" + std::to_string(session) +
            " ev=" + ui_mode_names[i] + " a=" + std::to_string(a) + " b=" +
            std::to_string(b) + " lost=0\r\n";
        assert(lines.back() == expected);
    }
    const char* const ui_boot_names[] = {
        "ui_init_begin", "ui_oled_ready", "ui_launcher_ready"
    };
    const char* const ui_boot_events[] = {
        "BW_UI_INIT_BEGIN", "BW_UI_OLED_READY", "BW_UI_LAUNCHER_READY"
    };
    for (unsigned i = 0; i < sizeof(ui_boot_names) / sizeof(ui_boot_names[0]); ++i) {
        const std::string value = std::string(ui_boot_names[i]) + " t=42 mode=1";
        message("boot", value.c_str(), uint16_t(30 + i)); poll();
        const std::string expected = "CRABIT:RADIO t=1 sid=" + std::to_string(30 + i) +
            " ev=" + ui_boot_events[i] + " a=1 b=0 lost=0\r\n";
        assert(lines.back() == expected);
    }
    message("boot", "ui_init_beginPRIVATE_TOKEN", 0); poll();
    assert(lines.back().find("ev=MSG_OTHER") != std::string::npos);
    assert(lines.back().find("BW_UI_INIT_BEGIN") == std::string::npos);
    for (const auto& line : lines) assert(line.find("PRIVATE_TOKEN") == std::string::npos);
    message("diag/radio", "BLE_ALIVE", 0); poll();
    assert(lines.back().find("ev=BW_BLE_ALIVE") != std::string::npos);
    message("diag/radio", "CMD_REJECTED", 7); poll();
    assert(lines.back().find("sid=7 ev=BW_CMD_REJECTED") != std::string::npos);
    message("sys/notify", "BW16 PONG OK!", 0); poll();
    assert(lines.back().find("ev=PONG") != std::string::npos);
    const size_t pong_count = lines.size();
    message("sys/notify", "BW16 PONG OK!PRIVATE_TOKEN", 0); poll();
    assert(lines.size() == pong_count);
    message("diag/radio", "UART_RX:28,1,0", 0); poll(); poll();
    assert(lines[lines.size()-2].find("ev=BW_RX_COUNTS a=28 b=1") != std::string::npos);
    assert(lines.back().find("ev=BW_BAD_FRAMES a=0 b=0") != std::string::npos);
    const size_t before = lines.size();
    message("diag/radio", "UART_RX:4294967296,0,0", 0); poll(); assert(lines.size() == before);
    message("diag/radio", "UART_RX:1,0,0PRIVATE_TOKEN", 0); poll(); assert(lines.size() == before);
    message("diag/echo", "PRIVATE_TOKEN", 7); poll(); assert(lines.size() == before);
    message("sys/comm", "PRIVATE_TOKEN", 7); poll();
    for (const auto& line : lines) assert(line.find("PRIVATE_TOKEN") == std::string::npos);
    busy = true; record(Start, 8); poll();
    assert(lines.size() == before + 1);
    busy = false; poll(); assert(lines.back().find("ev=START") != std::string::npos);
    for (unsigned i = 0; i < 60; ++i) record(Start, 9, i);
    for (unsigned i = 0; i < 60; ++i) poll();
    assert(lines.back().find("lost=12") != std::string::npos);
    busy = true; record(Start, 10); poll(); now += 1001; poll();
    busy = false; record(Start, 10); poll();
    assert(lines.back().find("lost=13") != std::string::npos);
    record(Start, 11); bw16_flash_mode = 1; poll(); record(Start, 11);
    const size_t after = lines.size(); bw16_flash_mode = 0; poll();
    assert(lines.size() == after && mask == 0);
}
""",
                encoding="utf-8",
            )
            binary = directory / "trace.exe"
            subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(directory),
                    "-I",
                    str(ROOT / "firmware/stm32/Core/Inc"),
                    str(ROOT / "firmware/stm32/Core/Src/companion_trace.cpp"),
                    str(fixture),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
