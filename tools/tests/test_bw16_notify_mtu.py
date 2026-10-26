"""Run the production notification sender with negotiated-MTU SDK doubles."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_bw16_uart_ingress import extract_function

ROOT = Path(__file__).resolve().parents[2]


class NotifyMtuTests(unittest.TestCase):
    def test_actual_sender_boundaries_fallback_and_link_guards(self):
        production = (ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.cpp").read_text(encoding="utf-8")
        source = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace RadioSvc { enum { CONNECTED=1 }; }
int g_state=1;
unsigned g_active_link_epoch=7, g_ble_link_epoch=7, g_tx_notify_epoch=7;
uint8_t g_conn_id=2, g_tx_notify_conn_id=2;
bool g_tx_notify_enabled=true;
const size_t RADIO_NOTIFY_MAX=320, RADIO_BLE_NOTIFY_MAX=230;
enum { GAP_PARAM_CONN_MTU_SIZE=1, GAP_CAUSE_SUCCESS=0 };
uint16_t negotiated=23;
bool queryFails=false, setFails=false, disconnectDuringDelay=false;
int delays=0;
int le_get_conn_param(int, void* out, uint8_t id) {
    assert(id==2); *static_cast<uint16_t*>(out)=negotiated;
    return queryFails ? 1 : GAP_CAUSE_SUCCESS;
}
#define pdMS_TO_TICKS(x) (x)
void vTaskDelay(int ms) { assert(ms==30); ++delays; if(disconnectDuringDelay) ++g_ble_link_epoch; }
struct Characteristic {
    std::string buffer;
    std::vector<std::string> packets;
    bool setData(uint8_t* bytes, size_t n) {
        assert(n<=230); buffer.assign(reinterpret_cast<char*>(bytes),n); return !setFails;
    }
    void notify(uint8_t id) { assert(id==2); packets.push_back(buffer); }
} g_tx_char;
"""
            + extract_function(production, "size_t notificationChunkBytes(uint8_t conn_id)")
            + "\n"
            + extract_function(production, "bool ownerNotify(const char* payload, uint8_t conn_id)")
            + r"""
int main() {
    const std::string payload(320,'x');
    for (uint16_t mtu : {23, 100, 233, 247, 517}) {
        negotiated=mtu; delays=0; g_tx_char.packets.clear();
        assert(ownerNotify(payload.c_str(),2));
        const size_t cap=mtu-3<230 ? mtu-3 : 230;
        assert(g_tx_char.packets.size()==(320+cap-1)/cap);
        std::string reconstructed;
        for(const auto& part:g_tx_char.packets) { assert(part.size()<=cap); reconstructed+=part; }
        assert(reconstructed==payload);
        assert(delays==static_cast<int>(g_tx_char.packets.size())-1);
    }
    queryFails=true; assert(notificationChunkBytes(2)==20); queryFails=false;
    negotiated=0; assert(notificationChunkBytes(2)==20);
    negotiated=23; disconnectDuringDelay=true; g_tx_char.packets.clear();
    assert(!ownerNotify(payload.c_str(),2)); assert(g_tx_char.packets.size()==1);
    g_ble_link_epoch=g_active_link_epoch; disconnectDuringDelay=false;
    setFails=true; g_tx_char.packets.clear(); assert(!ownerNotify("abc",2));
    assert(g_tx_char.packets.empty()); setFails=false;
    g_tx_notify_enabled=false; assert(!ownerNotify("abc",2));
    g_tx_notify_enabled=true; assert(!ownerNotify("abc",3));
    assert(!ownerNotify("",2)); assert(!ownerNotify(std::string(321,'x').c_str(),2));
}
"""
        )
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="crabit-notify-mtu-") as folder:
            cpp = Path(folder) / "test.cpp"
            binary = Path(folder) / "test.exe"
            cpp.write_text(source, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(cpp),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
