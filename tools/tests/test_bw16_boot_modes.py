"""Execute the product mode handlers with host-only SDK/UI doubles."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.tests.test_companion_uart_probe import function

ROOT = Path(__file__).resolve().parents[2]


class BootModeTest(unittest.TestCase):
    def test_actual_mode_handlers_select_once_and_preserve_ui_entry(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        services = (ROOT / "firmware/rtl8720dn/NanoUI/AmebaServices.h").read_text(encoding="utf-8")
        entry = (ROOT / "firmware/rtl8720dn/NanoUI/src/main_entry.cpp").read_text(encoding="utf-8")
        radio = (ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.cpp").read_text(encoding="utf-8")
        self.assertNotIn("xTaskCreate", radio)
        self.assertNotIn("xTaskNotify", radio)
        self.assertNotIn("millis() - boot_time > 5000", entry)
        self.assertIn('Protocol::send_message("diag/radio", "BOOT_READY")', entry)
        setup = function(entry, "void main_setup()")
        self.assertNotIn("BLE.init", setup)
        self.assertNotIn("start_ui_mode()", setup)
        self.assertNotIn("WiFi.begin", setup)
        fixture = r"""
#include "AmebaTypes.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
class String {
    std::string value;
public:
    String(const char* s): value(s ? s : "") {}
    String(std::string s): value(s) {}
    bool operator==(const char* s) const { return value == s; }
    bool startsWith(const char* s) const { return value.find(s) == 0; }
    int indexOf(char c, int start=0) const {
        auto pos=value.find(c, start); return pos==std::string::npos ? -1 : int(pos);
    }
    String substring(int start) const { return value.substr(start); }
    String substring(int start, int end) const { return value.substr(start,end-start); }
};
namespace Protocol {
    std::vector<std::string> replies;
    void send_message(const char*, const char* value) { replies.push_back(value); }
}
namespace RadioSvc {
    enum Result { QUEUED, BUSY, REJECTED };
    bool headless=false;
    bool companionControlsMode() { return headless; }
}
namespace NetManager {
    int starts=0, stops=0, wifi_on=0, wifi_off=0;
    uint16_t last_ble_request_id=0;
    RadioSvc::Result next=RadioSvc::QUEUED;
    RadioSvc::Result initBLE(uint16_t request_id=0) { ++starts; last_ble_request_id=request_id; return next; }
    void stopBLE() { ++stops; }
    void setWiFi(bool on, String = "", String = "") { on ? ++wifi_on : ++wifi_off; }
}
namespace AppManager { int exits=0; void exitCurrentApp() { ++exits; } }
int ui_starts=0, oled_releases=0, disconnects=0;
void start_ui_mode() { ++ui_starts; }
void releaseOledBus() { ++oled_releases; }
struct Wifi { void disconnect() { ++disconnects; } } WiFi;
volatile BW16Mode current_mode=MODE_WAITING;
bool is_display_active=false, ble_was_connected=false;
#define CRABIT_BW16_ENABLE_OTA 0
"""
        fixture += function(services, "static void applyNet(") + "\n"
        fixture += (
            function(services, "static void applyMode(")
            + r"""
void reset() {
    current_mode=MODE_WAITING; is_display_active=ble_was_connected=false;
    RadioSvc::headless=false; Protocol::replies.clear();
    NetManager::starts=NetManager::stops=NetManager::wifi_on=NetManager::wifi_off=0;
    NetManager::last_ble_request_id=0;
    NetManager::next=RadioSvc::QUEUED;
    ui_starts=oled_releases=disconnects=AppManager::exits=0;
}
void rejected() { assert(!Protocol::replies.empty() && Protocol::replies.back()=="REJECTED"); }
int main() {
    reset(); applyMode("UI"); assert(current_mode==MODE_UI && ui_starts==1);
    applyMode("BLE"); rejected(); assert(NetManager::starts==0 && current_mode==MODE_UI);
    applyMode("UI"); rejected(); assert(ui_starts==1);
    applyMode("WIFI"); rejected(); assert(current_mode==MODE_UI);
    is_display_active=true; applyMode("EXIT");
    assert(current_mode==MODE_SLEEP && oled_releases==1 && AppManager::exits==1);
    applyMode("UI"); rejected(); assert(ui_starts==1);
    reset(); applyMode("BLE"); assert(current_mode==MODE_BLE && NetManager::starts==1);
    assert(NetManager::last_ble_request_id==0); // Legacy/default invocation remains valid.
    applyMode("UI"); rejected(); assert(ui_starts==0);
    applyNet("wifi_on,ssid,pass"); rejected(); assert(NetManager::wifi_on==0);
    applyMode("BLE"); rejected(); assert(NetManager::starts==1);
    reset(); applyMode("WIFI"); assert(current_mode==MODE_WIFI);
    applyMode("BLE"); rejected(); assert(NetManager::starts==0);
    applyNet("wifi_on,ssid,pass"); assert(NetManager::wifi_on==1);
    applyNet("wifi_off"); assert(NetManager::wifi_off==1);
    reset(); applyNet("wifi_on,ssid,pass"); assert(current_mode==MODE_WIFI && NetManager::wifi_on==1);
    reset(); applyNet("wifi_off"); rejected(); assert(NetManager::wifi_off==0 && current_mode==MODE_WAITING);
    applyMode("EXIT"); rejected(); assert(disconnects==0 && current_mode==MODE_WAITING);
    applyMode("invalid"); rejected(); assert(current_mode==MODE_WAITING);
    applyMode("OTA"); assert(current_mode==MODE_WAITING);
    reset(); RadioSvc::headless=true; applyMode("UI"); rejected(); applyNet("wifi_on,ssid,pass"); rejected();
    assert(ui_starts==0 && NetManager::wifi_on==0 && disconnects==0);
    reset(); NetManager::next=RadioSvc::BUSY; applyMode("BLE"); assert(current_mode==MODE_WAITING);
    NetManager::next=RadioSvc::REJECTED; applyMode("BLE"); rejected(); assert(current_mode==MODE_WAITING);
    reset(); applyMode("BLE", 0xBEEF);
    assert(current_mode==MODE_BLE && NetManager::starts==1 && NetManager::last_ble_request_id==0xBEEF);
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-boot-modes-") as folder:
            source = Path(folder) / "modes.cpp"
            executable = Path(folder) / "modes.exe"
            source.write_text(fixture, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "firmware/rtl8720dn/NanoUI"),
                    str(source),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
