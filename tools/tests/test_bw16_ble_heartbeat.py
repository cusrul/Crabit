"""Execute the production BW16 boot diagnostic heartbeat with host protocol doubles."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def extract_function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for end in range(brace, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return text[start : end + 1]
    raise AssertionError(f"unterminated production function: {signature}")


class Bw16BleHeartbeatTests(unittest.TestCase):
    def test_actual_heartbeat_rate_wrap_wait_and_ui_paths(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "existing host g++ is required")
        main = (ROOT / "firmware/rtl8720dn/NanoUI/src/main_entry.cpp").read_text(encoding="utf-8")
        fixture = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
enum BW16Mode { MODE_WAITING, MODE_BLE, MODE_UI, MODE_SLEEP };
volatile BW16Mode current_mode = MODE_WAITING;
volatile bool is_display_active = false;
uint32_t now_ms = 0, boot_time = 0;
static uint32_t boot_diag_last_ms = 0, ui_alive_last_ms = 0;
static volatile uint32_t uart_trace_bytes = 27, uart_trace_frames = 3, uart_trace_bad = 1;
std::vector<std::string> sent;
uint32_t millis() { return now_ms; }
namespace Protocol {
void store_background_msg(const char* topic, const char* value) { sent.push_back(std::string(topic)+":"+value); }
void send_message(const char* topic, const char* value) { sent.push_back(std::string(topic)+":"+value); }
}
"""
            + extract_function(main, "void sendBootDiag(const char* stage)")
            + "\n"
            + extract_function(main, "void serviceBootDiag()")
            + r"""
int main() {
  current_mode=MODE_BLE; now_ms=1000; serviceBootDiag();
  assert(sent.size()==2 && sent[0]=="diag/radio:BLE_ALIVE" && sent[1].find("UART_RX:27,3,1") != std::string::npos);
  now_ms=1999; serviceBootDiag(); assert(sent.size()==2);
  now_ms=2000; serviceBootDiag(); assert(sent.size()==4 && sent[2]=="diag/radio:BLE_ALIVE");
  current_mode=MODE_WAITING; now_ms=2999; serviceBootDiag(); assert(sent.size()==4);
  now_ms=3000; serviceBootDiag(); assert(sent.size()==6 && sent[4].find("waiting") != std::string::npos);
  current_mode=MODE_UI; is_display_active=true; now_ms=999; serviceBootDiag(); assert(sent.size()==6);
  now_ms=1000; serviceBootDiag(); assert(sent.size()==7 && sent.back().find("ui_alive") != std::string::npos);
  current_mode=MODE_SLEEP; now_ms=5000; serviceBootDiag(); assert(sent.size()==7);
  boot_diag_last_ms=0xfffffff0u; current_mode=MODE_BLE; now_ms=0x000003d8u; serviceBootDiag();
  assert(sent.size()==9 && sent[7]=="diag/radio:BLE_ALIVE");
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-ble-heartbeat-") as folder:
            source, binary = Path(folder) / "heartbeat.cpp", Path(folder) / "heartbeat.exe"
            source.write_text(fixture, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DCRABIT_BW16_DIAG_ATTACH_TRACE=1",
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
