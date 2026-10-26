"""Host tests for BW16's mode-BLE RAM-only pairing policy and UART probe."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NANOUI = ROOT / "firmware/rtl8720dn/NanoUI"
RADIO = NANOUI / "RadioSvc.cpp"
SERVICES = NANOUI / "AmebaServices.h"
ENTRY = NANOUI / "src/main_entry.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start : i + 1]
    raise AssertionError(f"unterminated function: {signature}")


class Bw16ModeRamUartTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.radio = RADIO.read_text(encoding="utf-8")
        cls.services = SERVICES.read_text(encoding="utf-8")
        cls.entry = ENTRY.read_text(encoding="utf-8")
        cls.compiler = shutil.which("g++")

    def compile_and_run(self, program: str):
        self.assertIsNotNone(self.compiler, "g++ is required for production-body host fixtures")
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-ram-uart-") as folder:
            source, binary = Path(folder) / "fixture.cpp", Path(folder) / "fixture.exe"
            source.write_text(program, encoding="utf-8")
            built = subprocess.run(
                [
                    self.compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_connection_callback_advances_session_and_clears_notification_state(self):
        callback = function_body(self.radio, "T_APP_RESULT radioGapCallback(")
        program = (
            r"""
#include <cassert>
#include <cstdint>
using T_APP_RESULT = int;
constexpr int GAP_MSG_LE_CREATE_CONN_IND = 0x16;
constexpr int APP_RESULT_SUCCESS = 0, APP_RESULT_ACCEPT = 1;
volatile uint32_t g_ble_link_epoch = 7;
volatile bool g_tx_notify_enabled = true;
volatile uint8_t g_tx_notify_conn_id = 0;
"""
            + callback
            + r"""
int main() {
 assert(radioGapCallback(GAP_MSG_LE_CREATE_CONN_IND, nullptr) == APP_RESULT_ACCEPT);
 assert(g_ble_link_epoch == 8 && !g_tx_notify_enabled && g_tx_notify_conn_id == 0xff);
 assert(radioGapCallback(0x7f, nullptr) == APP_RESULT_SUCCESS);
}
"""
        )
        self.compile_and_run(program)

    def test_connection_indication_delivery_is_explicitly_enabled_in_sdk(self):
        callback = function_body(self.radio, "T_APP_RESULT radioGapCallback(")
        enable = function_body(self.radio, "bool enableConnectionIndicationCallback()")
        program = (
            r"""
#include <cassert>
#include <cstdint>
using T_APP_RESULT = int;
constexpr int GAP_MSG_LE_CREATE_CONN_IND=0x16, APP_RESULT_SUCCESS=0, APP_RESULT_ACCEPT=1;
constexpr int GAP_PARAM_HANDLE_CREATE_CONN_IND=0x228, GAP_CAUSE_SUCCESS=0;
volatile uint32_t g_ble_link_epoch=0;
volatile bool g_tx_notify_enabled=false;
volatile uint8_t g_tx_notify_conn_id=0xff;
bool registered=false, setting_called=false, setting_succeeded=true;
uint8_t callback_enabled=0;
void le_register_app_cb(T_APP_RESULT (*cb)(uint8_t,void*)) { registered=(cb!=nullptr); }
int le_set_gap_param(int param,uint8_t length,void* value) {
 assert(registered && param==GAP_PARAM_HANDLE_CREATE_CONN_IND && length==1);
 callback_enabled=*static_cast<uint8_t*>(value); setting_called=true;
 return setting_succeeded ? GAP_CAUSE_SUCCESS : 1;
}
"""
            + callback
            + "\n"
            + enable
            + r"""
int main() {
 assert(enableConnectionIndicationCallback());
 assert(registered && setting_called && callback_enabled==1);
 setting_called=false; setting_succeeded=false;
 assert(!enableConnectionIndicationCallback() && setting_called);
}
"""
        )
        self.compile_and_run(program)

    def test_new_connection_epoch_revokes_old_auth_before_accepting_writes(self):
        gap_callback = function_body(self.radio, "T_APP_RESULT radioGapCallback(")
        cccd_callback = function_body(
            self.radio,
            "void bleCccdCallback(BLECharacteristic*, uint8_t conn_id, uint16_t ccc_bits)",
        )
        clear = function_body(
            self.radio, "void clearConnectionState(bool reset_write_queue = true)"
        )
        begin_session = function_body(self.radio, "bool beginBleSessionForEpoch(")
        activate = function_body(self.radio, "void activateBleSessionForEvent(")
        connection = function_body(self.radio, "void processConnection()")
        notify = function_body(self.radio, "void processNotifyEvent(")
        program = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
namespace RadioSvc {
 enum State { ADVERTISING, CONNECTED };
 enum PairDiagnosticState { PAIR_DIAG_CANCELLED };
}
constexpr int GAP_MSG_LE_CREATE_CONN_IND=0x16, APP_RESULT_SUCCESS=0, APP_RESULT_ACCEPT=1;
constexpr uint16_t GATT_CLIENT_CHAR_CONFIG_NOTIFY=1;
constexpr uint32_t RADIO_AUTH_TIMEOUT_MS=15000;
constexpr uint16_t RADIO_NOTIFY_MAX=320;
using T_APP_RESULT = int;
struct BLECharacteristic {};
struct BleWriteEvent { uint8_t conn_id; uint16_t length; uint32_t link_epoch, overflow_epoch; uint8_t data[20]; };
struct NotifyEvent { uint8_t conn_id; uint16_t length; uint32_t link_epoch; char payload[320]; };
volatile uint32_t g_ble_link_epoch=10, g_rx_queue_overflow_count=0, g_auth_deadline_ms=0;
volatile uint32_t g_active_link_epoch=10, g_auth_disconnect_retry_ms=0;
volatile bool g_tx_notify_enabled=true, ble_is_authenticated=true, ble_was_connected=true;
volatile uint32_t g_tx_notify_epoch=10;
volatile bool g_auth_pending=false, g_auth_disconnect_pending=false, g_auth_disconnect_requested=false;
volatile uint8_t g_conn_id=0, g_tx_notify_conn_id=0;
volatile bool g_pair_diagnostic_active=false, g_companion_active=false, g_mode_ram_pairing=true;
bool g_rx_discard_until_command=false;
volatile RadioSvc::PairDiagnosticState g_pair_diagnostic_state=RadioSvc::PAIR_DIAG_CANCELLED;
uint16_t g_pair_rx_length=0, g_companion_session=0, g_request_id=31;
uint32_t g_rx_queue_overflow_seen=0, g_pair_deadline_ms=0, g_pending_pair_sequence=0;
uint8_t g_pair_conn_id=0;
std::string g_pending_pair_token;
char g_rx_buffer[513]="stale"; size_t g_rx_length=5;
void* g_event_queue=nullptr;
namespace NetManager { volatile uint8_t active_conn_id=0; }
uint32_t now=500; uint32_t millis(){return now;}
void xQueueReset(void*){}
bool connected=true;
struct FakeAdvert { void startAdv(){} } advert;
struct FakeBle { bool connected(){return ::connected;} bool connected(uint8_t){return ::connected;} FakeAdvert* configAdvert(){return &advert;} } BLE;
bool g_peripheral_started=false;
RadioSvc::State g_state=RadioSvc::ADVERTISING;
void setState(RadioSvc::State value){g_state=value;}
unsigned ready_count=0;
void publishBleStatus(const char* value,uint16_t){if(!strcmp(value,"READY")) ++ready_count;}
void publishCompanionStatus(const char*,uint16_t){}
void publishModePairStatus(const char*,uint32_t=0){}
void publishProtocolStatus(const char*,uint16_t){}
unsigned notify_calls=0;
bool ownerNotify(const char*,uint8_t){++notify_calls;return true;}
"""
            + clear
            + "\n"
            + begin_session
            + "\n"
            + gap_callback
            + "\n"
            + cccd_callback
            + "\n"
            + activate
            + r"""
"""
            + connection
            + "\n"
            + notify
            + r"""
int main(){
 assert(g_active_link_epoch==10 && ble_is_authenticated && g_tx_notify_enabled);
 assert(radioGapCallback(GAP_MSG_LE_CREATE_CONN_IND,nullptr)==APP_RESULT_ACCEPT);
 const uint32_t new_epoch=g_ble_link_epoch;
 assert(new_epoch==11 && !g_tx_notify_enabled && g_tx_notify_conn_id==0xff);
 bleCccdCallback(nullptr,0,GATT_CLIENT_CHAR_CONFIG_NOTIFY);
 processConnection(); // Link lifecycle must revoke auth even when no RX write arrives.
 assert(g_active_link_epoch==new_epoch && g_conn_id==0 && NetManager::active_conn_id==0);
 assert(g_state==RadioSvc::CONNECTED && ble_was_connected && !ble_is_authenticated);
 assert(g_auth_pending && g_auth_deadline_ms==now+RADIO_AUTH_TIMEOUT_MS);
 assert(g_tx_notify_enabled && g_tx_notify_conn_id==0 && ready_count==1);
 const uint32_t deadline=g_auth_deadline_ms;
 BleWriteEvent event{}; event.conn_id=0; event.length=1; event.link_epoch=new_epoch; event.overflow_epoch=0; event.data[0]='x';
 event.link_epoch=10; activateBleSessionForEvent(event);
 assert(g_active_link_epoch==new_epoch && g_auth_pending && !ble_is_authenticated);
 assert(g_auth_deadline_ms==deadline && ready_count==1);
 NotifyEvent old_notify{}; old_notify.conn_id=0; old_notify.length=1; old_notify.link_epoch=10;
 processNotifyEvent(old_notify); assert(notify_calls==0);
 NotifyEvent current_notify{}; current_notify.conn_id=0; current_notify.length=1; current_notify.link_epoch=new_epoch;
 processNotifyEvent(current_notify); assert(notify_calls==1);
}
"""
        )
        self.compile_and_run(program)

    def test_rx_overflow_drops_pre_loss_openers_until_a_fresh_command(self):
        prepare = function_body(self.radio, "bool prepareBleWriteEvent(const BleWriteEvent& event)")
        program = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstring>
constexpr unsigned RADIO_RX_BUFFER_MAX = 512;
constexpr uint8_t RADIO_JSON_OBJECT_OPEN = '{';
struct BleWriteEvent { uint8_t conn_id; uint16_t length; uint32_t link_epoch, overflow_epoch; uint8_t data[20]; };
volatile uint32_t g_ble_link_epoch=4, g_rx_queue_overflow_count=0;
uint32_t g_rx_queue_overflow_seen=0;
bool g_rx_discard_until_command=false, g_pair_diagnostic_active=false, connected=true;
uint16_t g_pair_rx_length=0;
char g_rx_buffer[RADIO_RX_BUFFER_MAX+1]="partial";
size_t g_rx_length=7;
struct FakeBle { bool connected(uint8_t) { return ::connected; } } BLE;
"""
            + prepare
            + r"""
int main() {
 BleWriteEvent pre_loss{}; pre_loss.length=1; pre_loss.link_epoch=4; pre_loss.overflow_epoch=0; pre_loss.data[0]='{';
 g_rx_queue_overflow_count=1;
 assert(!prepareBleWriteEvent(pre_loss));
 assert(g_rx_length==0 && g_rx_discard_until_command && g_rx_buffer[0]==0);
 BleWriteEvent continuation{}; continuation.length=2; continuation.link_epoch=4; continuation.overflow_epoch=1;
 continuation.data[0]='"'; continuation.data[1]='x';
 assert(!prepareBleWriteEvent(continuation) && g_rx_discard_until_command);
 BleWriteEvent fresh{}; fresh.length=1; fresh.link_epoch=4; fresh.overflow_epoch=1; fresh.data[0]='{';
 assert(prepareBleWriteEvent(fresh) && !g_rx_discard_until_command);
 BleWriteEvent stale_link=fresh; stale_link.link_epoch=3;
 assert(!prepareBleWriteEvent(stale_link));
 connected=false; assert(!prepareBleWriteEvent(fresh));
}
"""
        )
        self.compile_and_run(program)

        handler = function_body(self.radio, "void handleBleWriteEvent(const BleWriteEvent& event)")
        self.assertIn("activateBleSessionForEvent(event);", handler)

    def test_actual_configure_skips_preferences_only_under_mode_ram_lock(self):
        body = function_body(self.radio, "bool configureBleOnce(uint32_t generation)")
        program = (
            r"""
#include <cassert>
#include <cstdint>
#include <string>
unsigned prefs_begin=0, prefs_get=0, ble_init=0, event_clock=0;
unsigned prefs_order=0, init_order=0;
bool g_ble_stack_ready=false, g_mode_ram_pairing=false;
uint16_t g_request_id=8;
uint32_t active_generation=7;
const int GATT_PERM_READ=1, GATT_PERM_WRITE=2;
const uint16_t RADIO_BLE_NOTIFY_MAX=230;
const int TRACE_PREFS_BEGIN=1, TRACE_PREFS_DONE=2, TRACE_BLE_INIT_BEGIN=3, TRACE_BLE_INIT_DONE=4;
struct Prefs { void begin(const char*, bool) { ++prefs_begin; prefs_order=++event_clock; }
 std::string getString(const char*, const char*) { ++prefs_get; return "legacy-token"; } } g_pairing_prefs;
struct Bound { Bound& operator=(const char* s) { value=s; return *this; }
 Bound& operator=(const std::string& s) { value=s; return *this; } std::string value=""; } g_bound_pair_token;
struct Characteristic { void setReadProperty(bool){} void setReadPermissions(int){}
 int buffer_len=0;
 void setNotifyProperty(bool){} void setBufferLen(int len){buffer_len=len;} void setWriteProperty(bool){}
 void setWritePermissions(int){} template<class T> void setWriteCallback(T){}
 template<class T> void setCCCDCallback(T){} } g_tx_char,g_rx_char;
struct Service { void addCharacteristic(Characteristic&){} } g_uart_service;
struct BLEAdvertData { void addFlags(){} void addCompleteName(const char*){} };
struct Advert { void setAdvData(BLEAdvertData&){} } advert;
struct Ble { void init(){ ++ble_init; init_order=++event_clock; }
 Advert* configAdvert(){return &advert;} void configServer(int){}
 void addService(Service&){} void setDeviceName(const char*){} } BLE;
void bleWriteCallback(Characteristic*, uint8_t){}
void bleCccdCallback(Characteristic*, uint8_t, uint16_t){}
bool radioCheckpoint(int, uint16_t, uint32_t){return true;}
bool generationIsCurrent(uint32_t value){return value==active_generation;}
"""
            + body
            + r"""
int main() {
 g_mode_ram_pairing=true;
 assert(configureBleOnce(7));
 assert(prefs_begin==0 && prefs_get==0 && ble_init==1 && init_order!=0);
 assert(g_tx_char.buffer_len==230 && g_rx_char.buffer_len==20);
 assert(g_bound_pair_token.value.empty());
 g_ble_stack_ready=false; g_mode_ram_pairing=false; event_clock=0; prefs_order=init_order=0;
 assert(configureBleOnce(7));
 assert(prefs_begin==1 && prefs_get==1 && prefs_order<init_order);
 assert(g_bound_pair_token.value=="legacy-token");
}
"""
        )
        self.compile_and_run(program)

    def test_static_process_start_policy_order_and_pairing_lock_survives_diagnostic_end(self):
        start = function_body(self.radio, "void processStart(const RadioCommand& command)")
        self.assertLess(
            start.index("g_mode_ram_pairing = true"), start.index("processPairDiagnosticBegin()")
        )
        self.assertLess(
            start.index("processPairDiagnosticBegin()"), start.index('g_bound_pair_token = ""')
        )
        self.assertLess(
            start.index('g_bound_pair_token = ""'),
            start.index("configureBleOnce(command.generation)"),
        )
        self.assertLess(
            start.index("g_mode_ram_pairing = true"), start.index("BLE.beginPeripheral()")
        )

        signatures = (
            "void clearPendingPair()",
            "void processPairing()",
            "void processPairDiagnosticBegin()",
            "void processPairDiagnosticCancel()",
            "void processPairDiagnosticEnd()",
        )
        functions = "\n".join(function_body(self.radio, sig) for sig in signatures)
        program = (
            r"""
#include <cassert>
#include <cstdint>
#include <string>
namespace RadioSvc {
 enum State { CONNECTED };
 enum PairDiagnosticState { PAIR_DIAG_OFF, PAIR_DIAG_IDLE, PAIR_DIAG_TIMEOUT, PAIR_DIAG_CANCELLED };
 inline bool pairDeadlineExpired(uint32_t now,uint32_t deadline){return int32_t(now-deadline)>=0;}
}
std::string g_pending_pair_token, g_bound_pair_token="old-token";
uint32_t g_pair_deadline_ms=1000, g_pending_pair_sequence=55;
uint8_t g_pair_conn_id=2, g_conn_id=2;
uint16_t g_pair_rx_length=9, g_companion_session=3;
bool g_mode_ram_pairing=false, g_pair_diagnostic_active=false, g_companion_active=false;
bool ble_is_authenticated=true;
RadioSvc::State g_state=RadioSvc::CONNECTED;
RadioSvc::PairDiagnosticState g_pair_diagnostic_state=RadioSvc::PAIR_DIAG_OFF;
unsigned prefs_writes=0, owner_events=0;
struct Prefs { void putString(const char*,const std::string&){++prefs_writes;} } g_pairing_prefs;
const int BTN_OK_PIN=1, LOW=0;
int digitalRead(int){return LOW;} uint32_t millis(){return 10;}
void ownerNotify(const char*,uint8_t){++owner_events;}
void publishCompanionStatus(const char*,uint16_t){++owner_events;}
void publishModePairStatus(const char*,uint32_t=0){++owner_events;}
"""
            + functions
            + r"""
int main(){
 g_mode_ram_pairing=true; g_bound_pair_token=""; processPairDiagnosticBegin();
 assert(g_pair_diagnostic_active && !ble_is_authenticated && g_pending_pair_token.empty());
 g_pending_pair_token="RAM-candidate"; g_pair_deadline_ms=1000; processPairing();
 assert(prefs_writes==0 && !ble_is_authenticated && g_pending_pair_token=="RAM-candidate");
 processPairDiagnosticEnd();
 assert(g_mode_ram_pairing && g_pair_diagnostic_active && g_pending_pair_token=="RAM-candidate");
 processPairing(); assert(prefs_writes==0 && !ble_is_authenticated);
 // Control: the unchanged legacy branch still persists on physical OK.
 g_mode_ram_pairing=false; g_pair_diagnostic_active=false;
 g_pending_pair_token="legacy-candidate"; processPairing();
 assert(prefs_writes==1 && ble_is_authenticated && g_bound_pair_token=="legacy-candidate");
}
"""
        )
        self.compile_and_run(program)

    def test_actual_process_start_body_sets_mode_ble_policy_before_configure_and_radio(self):
        # Executes the exact production processStart body. configureBleOnce and
        # vendor BLE/GAP methods are narrow doubles; the configure double
        # asserts the policy snapshot at call entry.
        start = function_body(self.radio, "void processStart(const RadioCommand& command)")
        clear = function_body(self.radio, "void clearPendingPair()")
        begin = function_body(self.radio, "void processPairDiagnosticBegin()")
        program = (
            r"""
#include <cassert>
#include <cstdint>
#include <string>
namespace RadioSvc {
 enum State { IDLE, ADVERTISING, FAILED };
 enum Result { ACQUIRED, FAILED_RESULT };
 enum PairDiagnosticState { PAIR_DIAG_OFF, PAIR_DIAG_IDLE };
 void flushUartEvents() {}
}
enum Mode { MODE_WAITING, MODE_BLE, MODE_UI };
enum { TRACE_PERIPH_BEGIN=1, TRACE_PERIPH_DONE=2, GAP_PARAM_DEV_STATE=3,
 GAP_PARAM_HANDLE_CREATE_CONN_IND=4, GAP_CAUSE_SUCCESS=0, GAP_ADV_STATE_ADVERTISING=4 };
struct RadioCommand { int kind; uint16_t request_id; uint32_t generation; uint32_t pair_seq; };
struct GapDeviceState { uint8_t gap_adv_state=GAP_ADV_STATE_ADVERTISING; };
using T_GAP_DEV_STATE=GapDeviceState;
struct Bound { std::string value="saved-token"; Bound& operator=(const char* s){value=s;return *this;}
 bool empty() const{return value.empty();} } g_bound_pair_token;
std::string g_pending_pair_token;
volatile int current_mode=MODE_BLE;
bool g_mode_ram_pairing=false, g_pair_diagnostic_active=false, ble_is_authenticated=true;
bool g_companion_active=false;
uint16_t g_mode_pair_session=0;
RadioSvc::PairDiagnosticState g_pair_diagnostic_state=RadioSvc::PAIR_DIAG_OFF;
bool g_peripheral_started=false;
uint16_t g_pair_rx_length=12, g_pair_conn_id=1;
uint32_t g_pair_deadline_ms=99, g_pending_pair_sequence=17;
uint32_t g_generation=7, g_start_deadline_ms=100, active_generation=7;
uint16_t g_request_id=33;
RadioSvc::Result g_last_result=RadioSvc::FAILED_RESULT;
RadioSvc::State g_state=RadioSvc::IDLE;
unsigned configure_calls=0, begin_calls=0, adv_calls=0, pref_accesses=0;
bool configure_saw_ram_policy=false;
uint32_t now=10;
unsigned gap_callback_enable_calls=0;
bool gap_callback_enable_ok=true;
bool isConfiguredAuthCodeValid(){return true;}
int radioGapCallback(uint8_t, void*){return 0;}
void le_register_app_cb(int (*)(uint8_t, void*)){}
int le_set_gap_param(int,uint8_t,void*){return GAP_CAUSE_SUCCESS;}
bool enableConnectionIndicationCallback(){++gap_callback_enable_calls;return gap_callback_enable_ok;}
"""
            + clear
            + r"""
"""
            + begin
            + r"""
bool generationIsCurrent(uint32_t generation){return generation==active_generation;}
void failGeneration(uint32_t,const char*){assert(false && "unexpected failure path");}
bool radioCheckpoint(int,uint16_t,uint32_t){return true;}
bool configureBleOnce(uint32_t) {
 ++configure_calls;
 configure_saw_ram_policy = g_mode_ram_pairing && g_pair_diagnostic_active && g_bound_pair_token.empty();
 if (!g_mode_ram_pairing) ++pref_accesses;
 return true;
}
struct Advert { void startAdv(){++adv_calls; assert(g_mode_ram_pairing || current_mode==MODE_UI);}
 void stopAdv(){} } advert;
struct Ble { void beginPeripheral(){++begin_calls; ++adv_calls;
 assert(g_mode_ram_pairing || current_mode==MODE_UI);}
 Advert* configAdvert(){return &advert;} bool connected(){return false;} } BLE;
uint32_t millis(){return now;} void delay(uint32_t){}
int le_get_gap_param(int,GapDeviceState* out){out->gap_adv_state=GAP_ADV_STATE_ADVERTISING;return GAP_CAUSE_SUCCESS;}
void setState(RadioSvc::State value){g_state=value;}
void publishProtocolStatus(const char*,uint16_t){}
void publishModePairStatus(const char*,uint32_t=0){}
"""
            + start
            + r"""
static void reset() {
 current_mode=MODE_BLE; g_mode_ram_pairing=false; g_pair_diagnostic_active=false;
 g_companion_active=false; g_mode_pair_session=0;
 g_bound_pair_token="saved-token"; g_pending_pair_token="candidate";
 ble_is_authenticated=true; g_pair_rx_length=12; g_pair_conn_id=1;
 g_pair_deadline_ms=99; g_pending_pair_sequence=17;
 g_generation=active_generation=7; g_request_id=33; g_start_deadline_ms=100;
 g_peripheral_started=false; configure_calls=begin_calls=adv_calls=pref_accesses=0;
 configure_saw_ram_policy=false; g_state=RadioSvc::IDLE; now=10;
}
int main() {
 reset();
 processStart({0,33,6,0}); // stale generation has no policy/SDK side effects
 assert(!g_mode_ram_pairing && !g_pair_diagnostic_active && g_bound_pair_token.value=="saved-token");
 assert(configure_calls==0 && begin_calls==0 && adv_calls==0);

 reset(); processStart({0,33,7,0});
 assert(g_mode_ram_pairing && g_pair_diagnostic_active && g_bound_pair_token.empty());
 assert(g_mode_pair_session==33);
 assert(configure_calls==1 && configure_saw_ram_policy && pref_accesses==0);
 assert(begin_calls==1 && adv_calls==1 && g_state==RadioSvc::ADVERTISING);
 assert(!g_companion_active);

 reset(); current_mode=MODE_UI; processStart({0,33,7,0});
 assert(!g_mode_ram_pairing && !g_pair_diagnostic_active);
 assert(g_mode_pair_session==0);
 assert(configure_calls==1 && !configure_saw_ram_policy && pref_accesses==1);
 assert(begin_calls==1 && adv_calls==1 && g_state==RadioSvc::ADVERTISING);
}
"""
        )
        self.compile_and_run(program)

    def test_uart_probe_actual_enqueue_and_owner_response_guards(self):
        enqueue = function_body(
            self.services, "static bool enqueueUartProbe(const CrabitWire::Message& message)"
        )
        program = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstring>
namespace CrabitWire { struct Message { uint16_t request_id; char value[32]; }; }
struct PendingCommand { uint8_t kind; uint16_t request_id; char value[96]; } last{};
bool queue_accept=true; unsigned queue_calls=0;
bool enqueue(uint8_t kind,const char* value,uint16_t id=0) {
 ++queue_calls; last.kind=kind; last.request_id=id;
 std::strncpy(last.value,value?value:"",sizeof(last.value)-1); return queue_accept;
}
struct TestAmebaServices {
"""
            + enqueue
            + r"""
};
int main(){
 CrabitWire::Message m{43,"BLE_UART"};
 assert(TestAmebaServices::enqueueUartProbe(m));
 assert(queue_calls==1 && last.kind==3 && last.request_id==43 && !strcmp(last.value,"BLE_UART"));
 m.request_id=0; assert(!TestAmebaServices::enqueueUartProbe(m) && queue_calls==1);
 m.request_id=44; std::strcpy(m.value,"PING");
 assert(!TestAmebaServices::enqueueUartProbe(m) && queue_calls==1);
 std::strcpy(m.value,"BLE_UART"); queue_accept=false;
 assert(!TestAmebaServices::enqueueUartProbe(m) && queue_calls==2 && last.request_id==44);
}
"""
        )
        self.compile_and_run(program)

        # Static routing/response checks: these are not executable evidence.
        worker = function_body(self.entry, "void Task_UART_Worker(void *pvParameters)")
        self.assertIn('strcmp(message.topic, "sys/bw/ping") == 0 && message.request_id', worker)
        self.assertIn('strcmp(message.value, "BLE_UART") == 0', worker)
        self.assertIn("AmebaServices::enqueueUartProbe(message)", worker)
        self.assertIn("else AmebaEventBus::dispatch(message.topic, message.value);", worker)
        service = function_body(self.services, "static void service()")
        self.assertIn('strcmp(command.value, "BLE_UART") == 0', service)
        self.assertIn("radio.ram_pairing_only", service)
        self.assertIn(
            "radio.state == RadioSvc::ADVERTISING || radio.state == RadioSvc::CONNECTED", service
        )
        self.assertIn('ready ? "UART_OK" : "UART_NOT_READY"', service)
        self.assertIn(
            'Protocol::send_message("sys/comm", ready ? "UART_OK" : "UART_NOT_READY",', service
        )
        self.assertIn('} else Protocol::send_message("sys/notify", "BW16 PONG OK!");', service)
        self.assertNotIn("g_companion_active", service)
        self.assertNotIn("RadioSvc::enqueueCompanion", service)
        header = (NANOUI / "RadioSvc.h").read_text(encoding="utf-8")
        self.assertIn("bool ram_pairing_only;", header)
        self.assertIn("result.ram_pairing_only = g_mode_ram_pairing;", self.radio)


if __name__ == "__main__":
    unittest.main()
