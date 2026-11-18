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
        self.assertIn('CrabitLink::send(Topics::DiagRadio, "BOOT_READY")', entry)
        setup = function(entry, "void main_setup()")
        self.assertNotIn("BLE.init", setup)
        self.assertNotIn("start_ui_mode()", setup)
        self.assertNotIn("WiFi.begin", setup)

        # UI readiness must follow the panel claim, not precede it.
        ui_start = function(entry, "bool start_ui_mode()")
        hardware = ui_start.index("canvas.initHardware();")
        bind = ui_start.index("AppManager::init(&canvas, &input);")
        launch = ui_start.index("AppManager::launch(new LauncherApp());")
        self.assertLess(hardware, bind)
        self.assertLess(bind, launch)
        self.assertIn("return is_display_active;", ui_start)
        # Correlated UI commands are routed to the request-id-aware handler.
        worker = function(entry, "void Task_UART_Worker(")
        self.assertIn('strcmp(message.value, "UI") == 0', worker)

        fixture = r"""
#include "AmebaTypes.h"
#include <cassert>
#include <cstdint>
#include <cstring>
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
struct Topics {
    static constexpr const char* SysAck = "sys/ack";
    static constexpr const char* SysNotify = "sys/notify";
};
std::vector<std::string> order;
namespace CrabitLink {
    std::vector<std::string> topics, values, ids;
    bool send(const char* topic, const char* value, uint16_t id=0) {
        topics.push_back(topic ? topic : "");
        values.push_back(value ? value : "");
        ids.push_back(std::to_string((unsigned)id));
        order.push_back(std::string("send:") + (value ? value : "") + ":" + std::to_string((unsigned)id));
        return true;
    }
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
namespace AppManager {
    int exits=0;
    void shutdown() { ++exits; order.push_back("shutdown"); }
    void exitCurrentApp() { ++exits; order.push_back("exit"); }
}
int ui_starts=0, oled_releases=0, disconnects=0;
bool ui_ready_result=true;
volatile BW16Mode current_mode=MODE_WAITING;
bool is_display_active=false, ble_was_connected=false;
bool start_ui_mode() {
    ++ui_starts;
    order.push_back("ui_start");
    is_display_active = ui_ready_result;
    return ui_ready_result;
}
void releaseOledBus() { ++oled_releases; order.push_back("release"); }
struct Wifi { void disconnect() { ++disconnects; } } WiFi;
#define CRABIT_BW16_ENABLE_OTA 0
"""
        fixture += function(services, "static void applyNet(") + "\n"
        fixture += (
            function(services, "static void applyMode(")
            + r"""
void reset() {
    current_mode=MODE_WAITING; is_display_active=ble_was_connected=false;
    RadioSvc::headless=false; ui_ready_result=true;
    CrabitLink::topics.clear(); CrabitLink::values.clear(); CrabitLink::ids.clear();
    order.clear();
    NetManager::starts=NetManager::stops=NetManager::wifi_on=NetManager::wifi_off=0;
    NetManager::last_ble_request_id=0;
    NetManager::next=RadioSvc::QUEUED;
    ui_starts=oled_releases=disconnects=AppManager::exits=0;
}
void rejected(uint16_t id=0) {
    assert(!CrabitLink::values.empty() && CrabitLink::values.back()=="REJECTED");
    assert(CrabitLink::ids.back()==std::to_string((unsigned)id));
}
int main() {
    // Legacy request_id == 0 still starts UI and sends no correlated answer.
    reset(); applyMode("UI");
    assert(current_mode==MODE_UI && ui_starts==1 && CrabitLink::values.empty());
    applyMode("BLE"); rejected(); assert(NetManager::starts==0 && current_mode==MODE_UI);
    // Legacy UI outside MODE_WAITING stays silent: no new uncorrelated traffic.
    CrabitLink::values.clear(); CrabitLink::ids.clear();
    applyMode("UI"); assert(CrabitLink::values.empty()); assert(ui_starts==1);
    applyMode("WIFI"); rejected(); assert(current_mode==MODE_UI);
    is_display_active=true; order.clear(); applyMode("EXIT");
    assert(current_mode==MODE_SLEEP && oled_releases==1 && AppManager::exits==1);
    assert(order.size()>=3 && order[0]=="shutdown" && order[1]=="release" &&
           order[2]=="send:BW16_EXIT:0");
    CrabitLink::values.clear(); CrabitLink::ids.clear();
    applyMode("UI"); assert(CrabitLink::values.empty()); assert(ui_starts==1);
    reset(); applyMode("BLE"); assert(current_mode==MODE_BLE && NetManager::starts==1);
    assert(NetManager::last_ble_request_id==0); // Legacy/default invocation remains valid.
    CrabitLink::values.clear(); CrabitLink::ids.clear();
    applyMode("UI"); assert(CrabitLink::values.empty()); assert(ui_starts==0);
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
    reset(); RadioSvc::headless=true; applyMode("UI", 0x4444); rejected(0x4444);
    assert(ui_starts==0);
    reset(); NetManager::next=RadioSvc::BUSY; applyMode("BLE"); assert(current_mode==MODE_WAITING);
    NetManager::next=RadioSvc::REJECTED; applyMode("BLE"); rejected(); assert(current_mode==MODE_WAITING);
    reset(); applyMode("BLE", 0xBEEF);
    assert(current_mode==MODE_BLE && NetManager::starts==1 && NetManager::last_ble_request_id==0xBEEF);

    // New handshake: the answer is echoed only after start_ui_mode() succeeded.
    reset(); applyMode("UI", 0x1234);
    assert(current_mode==MODE_UI && ui_starts==1);
    assert(CrabitLink::values.back()=="UI_READY" && CrabitLink::ids.back()=="4660");
    applyMode("UI", 0x2222);
    rejected(0x2222);
    // A failed panel claim is correlated and does not notify legacy firmware.
    reset(); ui_ready_result=false; applyMode("UI", 0x2222);
    assert(current_mode==MODE_UI && CrabitLink::values.back()=="UI_FAILED" &&
           CrabitLink::ids.back()=="8738" && oled_releases==0);
    // Correlated exit: app cleanup, then bus release, then the answer.
    reset(); current_mode=MODE_UI; is_display_active=true; applyMode("EXIT", 0x3333);
    assert(current_mode==MODE_SLEEP && CrabitLink::values.back()=="UI_EXIT" &&
           CrabitLink::ids.back()=="13107");
    assert(order.size()>=3 && order[0]=="shutdown" && order[1]=="release" &&
           order[2]=="send:UI_EXIT:13107");
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
