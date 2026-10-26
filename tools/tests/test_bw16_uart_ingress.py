"""Execute the production BW16 UART worker and companion admission path on host doubles."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_companion_uart_probe import function


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


class Bw16UartIngressTests(unittest.TestCase):
    def test_actual_worker_wire_decode_and_queue_admission(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "existing host g++ is required")
        main = (ROOT / "firmware/rtl8720dn/NanoUI/src/main_entry.cpp").read_text(encoding="utf-8")
        radio_h = (ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.h").read_text(encoding="utf-8")
        radio_cpp = (ROOT / "firmware/rtl8720dn/NanoUI/RadioSvc.cpp").read_text(encoding="utf-8")
        services = (ROOT / "firmware/rtl8720dn/NanoUI/AmebaServices.h").read_text(encoding="utf-8")

        production = (
            r"""
#include "crabit_wire.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
using namespace std;
using QueueHandle_t = struct FakeQueue*;
using TaskHandle_t = void*;
constexpr int pdTRUE = 1;
#define pdMS_TO_TICKS(x) (x)
struct FakeQueue { size_t item_size; size_t capacity; deque<vector<uint8_t>> items; };
QueueHandle_t xQueueCreate(size_t capacity, size_t item_size) { return new FakeQueue{item_size, capacity, {}}; }
int xQueueSend(QueueHandle_t q, const void* item, unsigned) {
    if (!q || q->items.size() == q->capacity) return 0;
    vector<uint8_t> copy(q->item_size);
    memcpy(copy.data(), item, q->item_size);
    q->items.push_back(copy);
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void* item, unsigned) {
    if (!q || q->items.empty()) return 0;
    auto copy = q->items.front(); q->items.pop_front();
    memcpy(item, copy.data(), q->item_size); return pdTRUE;
}
"""
            + extract_function(services, "struct PendingCommand")
            + ";\n"
            + r"""
struct PendingNotification { uint16_t request_id; char topic[CrabitWire::MAX_TOPIC+1]; char value[CrabitWire::MAX_VALUE+1]; };
struct PendingSensors { int imu[3], pat[3], battery, buttons; };
struct PendingWifiDiagnostic { uint16_t request_id; uint8_t conn_id; };
enum CommandKind : uint8_t { COMMAND_START=1, COMMAND_STOP=2, COMMAND_PAIR_DIAG_BEGIN=3,
 COMMAND_PAIR_DIAG_CONFIRM_RAM=4, COMMAND_PAIR_DIAG_CANCEL=5, COMMAND_PAIR_DIAG_END=6,
 COMMAND_COMPANION_START=7, COMMAND_COMPANION_STOP=8, COMMAND_COMPANION_CONFIRM=9, COMMAND_COMPANION_CANCEL=10,
 COMMAND_COMPANION_ATTACH=11 };
"""
            + "\n"
            + extract_function(radio_cpp, "struct RadioCommand")
            + ";\n"
            + extract_function(radio_cpp, "enum UartEventKind : uint8_t")
            + ";\n"
            + r"""
static const uint16_t RADIO_NOTIFY_MAX=320;
"""
            + extract_function(radio_cpp, "struct UartEvent")
            + ";\n"
            + r"""
static const uint8_t RADIO_COMMAND_QUEUE_LENGTH=4, RADIO_UART_QUEUE_LENGTH=8;
QueueHandle_t command_queue, notification_queue, sensor_queue, diagnostic_queue, g_command_queue, g_uart_queue;
bool g_owner_ready=true; volatile uint32_t g_generation=0; unsigned trace_rx=0;
deque<vector<uint8_t>> delayed_chunks;
bool assert_no_frame_before_delayed_tail = false;
unsigned uart_trace_bytes=0, uart_trace_frames=0, uart_trace_bad=0;
struct FakeSerial { deque<uint8_t> input; int available(){return (int)input.size();} int read(){int b=input.front();input.pop_front();return b;} } Serial;
void vTaskDelay(unsigned) {
    if (!delayed_chunks.empty()) {
        if (assert_no_frame_before_delayed_tail) {
            assert(uart_trace_frames==3 && trace_rx==2);
            assert_no_frame_before_delayed_tail=false;
        }
        auto chunk=delayed_chunks.front(); delayed_chunks.pop_front();
        Serial.input.assign(chunk.begin(),chunk.end()); return;
    }
    throw runtime_error("bounded worker iteration complete");
}
namespace RadioSvc {
enum Result { QUEUED, ACQUIRED, BUSY, REJECTED, FAILED_RESULT, STOPPED_RESULT, TIMEOUT_RESULT };
"""
            + function(radio_h, "inline bool parseCompanionValue(")
            + r"""
void traceCompanionReceived(uint16_t) { ++trace_rx; }
Result enqueueCompanion(uint16_t session_id, const char* action, uint32_t pair_seq);
}
RadioSvc::Result g_last_result=RadioSvc::REJECTED;
"""
            + function(radio_cpp, "void publishCompanionStatus(")
            + r"""
"""
            + "namespace RadioSvc {\n"
            + function(radio_cpp, "Result enqueueCompanion(")
            + "\n}\n"
            + r"""
class AmebaServices {
public:
"""
            + function(services, "static bool enqueue(uint8_t kind,")
            + r"""
"""
            + function(services, "static bool enqueueUartProbe(")
            + r"""
"""
            + function(services, "static bool enqueueCompanion(")
            + r"""
"""
            + function(services, "static bool enqueueNotification(")
            + r"""
"""
            + function(services, "static bool enqueueMode(")
            + r"""
"""
            + function(services, "static bool enqueueModePair(")
            + r"""
};
struct AmebaEventBus { static void dispatch(const char* topic, const char*) {
    if (!strcmp(topic, "sys/bw/ping")) AmebaServices::enqueue(3, "");
} };
namespace CrabitWire { }
namespace AuditRelay {
static unsigned received = 0;
static uint16_t lastRequest = 0;
void receive(const CrabitWire::Message& message) { ++received; lastRequest=message.request_id; }
}
"""
            + extract_function(main, "void Task_UART_Worker(")
            + r"""
vector<uint8_t> frame(const char* topic, const char* value, uint16_t id) {
    uint8_t payload[CrabitWire::PAYLOAD_SIZE], bytes[CrabitWire::FRAME_SIZE]; size_t n=0,m=0;
    assert(CrabitWire::encode_message(topic,value,id,payload,sizeof(payload),&n));
    assert(CrabitWire::encode_frame(payload,n,bytes,sizeof(bytes),&m)); return {bytes,bytes+m};
}
void run(const vector<uint8_t>& bytes) {
    delayed_chunks.clear();
    Serial.input.assign(bytes.begin(),bytes.end());
    try { Task_UART_Worker(nullptr); } catch (const runtime_error&) {}
    assert(Serial.input.empty());
}
void runFragmented(const vector<uint8_t>& bytes, size_t split) {
    assert(split>0 && split<bytes.size()); delayed_chunks.clear();
    assert_no_frame_before_delayed_tail=true;
    Serial.input.assign(bytes.begin(),bytes.begin()+split);
    delayed_chunks.push_back(vector<uint8_t>(bytes.begin()+split,bytes.end()));
    try { Task_UART_Worker(nullptr); } catch (const runtime_error&) {}
    assert(Serial.input.empty() && delayed_chunks.empty());
}
int main() {
    command_queue=xQueueCreate(8,sizeof(PendingCommand)); notification_queue=xQueueCreate(8,sizeof(PendingNotification));
    sensor_queue=xQueueCreate(8,sizeof(PendingSensors)); diagnostic_queue=xQueueCreate(4,sizeof(PendingWifiDiagnostic));
    g_command_queue=xQueueCreate(RADIO_COMMAND_QUEUE_LENGTH,sizeof(RadioCommand)); g_uart_queue=xQueueCreate(RADIO_UART_QUEUE_LENGTH, sizeof(UartEvent));
    auto ping=frame("sys/bw/ping","PING",0); run(ping);
    assert(uart_trace_frames==1 && uart_trace_bad==0 && trace_rx==0 && g_command_queue->items.empty());
    assert(command_queue->items.size()==1 && command_queue->items.front().size()==sizeof(PendingCommand));
    PendingCommand pc{}; xQueueReceive(command_queue,&pc,0); assert(pc.kind==3 && !strcmp(pc.value,""));

    auto start=frame("sys/comm","START",1); run(start);
    assert(uart_trace_frames==2 && trace_rx==1 && command_queue->items.empty());
    assert(g_command_queue->items.size()==1 && g_command_queue->items.front().size()==sizeof(RadioCommand));
    RadioCommand rc{}; xQueueReceive(g_command_queue,&rc,0);
    assert(rc.kind==COMMAND_COMPANION_START && rc.request_id==1 && rc.generation==1 && rc.pair_seq==0);
    assert(g_uart_queue->items.size()==1 && g_uart_queue->items.front().size()==sizeof(UartEvent));
    UartEvent status1{}; xQueueReceive(g_uart_queue,&status1,0);
    assert(status1.kind==UART_COMPANION_STATUS && status1.request_id==1 && !strcmp(status1.payload,"QUEUED"));

    auto invalid=frame("sys/comm","START",0); run(invalid);
    assert(uart_trace_frames==3 && trace_rx==2 && g_command_queue->items.empty());
    assert(g_uart_queue->items.empty()); // AmebaServices rejects zero ID before RadioSvc

    auto fragmented=frame("sys/comm","START",2); runFragmented(fragmented,7);
    assert(uart_trace_frames==4 && trace_rx==3 && g_command_queue->items.size()==1);
    RadioCommand fragmented_command{}; xQueueReceive(g_command_queue,&fragmented_command,0);
    assert(fragmented_command.kind==COMMAND_COMPANION_START && fragmented_command.request_id==2);
    UartEvent status2{}; xQueueReceive(g_uart_queue,&status2,0);
    assert(status2.kind==UART_COMPANION_STATUS && status2.request_id==2 && !strcmp(status2.payload,"QUEUED"));
    auto truncated=fragmented; truncated.pop_back(); run(truncated);
    auto bad_crc=frame("sys/comm","START",3); bad_crc.back()^=0x80; run(bad_crc);
    assert(uart_trace_frames==4 && uart_trace_bad==1 && trace_rx==3 && g_command_queue->items.empty());

    for (int i=0;i<RADIO_COMMAND_QUEUE_LENGTH;i++) { RadioCommand fill{COMMAND_STOP,(uint16_t)i,0,0}; assert(xQueueSend(g_command_queue,&fill,0)==pdTRUE); }
    auto full=frame("sys/comm","START",4); run(full);
    assert(g_command_queue->items.size()==RADIO_COMMAND_QUEUE_LENGTH && uart_trace_frames==5 && trace_rx==4);
    UartEvent rejected{}; xQueueReceive(g_uart_queue,&rejected,0);
    assert(rejected.kind==UART_COMPANION_STATUS && rejected.request_id==4 && !strcmp(rejected.payload,"REJECTED"));
    g_owner_ready=false;
    auto owner_missing=frame("sys/comm","START",5); run(owner_missing);
    assert(g_command_queue->items.size()==RADIO_COMMAND_QUEUE_LENGTH && uart_trace_frames==6 && trace_rx==5);
    UartEvent not_ready{}; xQueueReceive(g_uart_queue,&not_ready,0);
    assert(not_ready.kind==UART_COMPANION_STATUS && not_ready.request_id==5 && !strcmp(not_ready.payload,"REJECTED"));

    // Exercise the exact post-start ATTACH wire shape, not only cold START.
    while (xQueueReceive(g_command_queue,&rc,0)==pdTRUE) {}
    g_owner_ready=true; g_generation=9;
    auto attach=frame("sys/comm","ATTACH",77); run(attach);
    assert(attach.size()==23 && uart_trace_frames==7 && trace_rx==6);
    assert(g_command_queue->items.size()==1 && command_queue->items.empty());
    RadioCommand attached_command{}; xQueueReceive(g_command_queue,&attached_command,0);
    assert(attached_command.kind==COMMAND_COMPANION_ATTACH && attached_command.request_id==77);
    assert(attached_command.generation==9 && attached_command.pair_seq==0);
    UartEvent attach_queued{}; xQueueReceive(g_uart_queue,&attach_queued,0);
    assert(attach_queued.kind==UART_COMPANION_STATUS && attach_queued.request_id==77 && !strcmp(attach_queued.payload,"QUEUED"));

    // The new post-BLE check is a CrabitWire UART frame, not a local BLE TEST
    // or a companion command. Preserve its request id in the ordinary queue.
    auto probe=frame("sys/bw/ping","BLE_UART",101); run(probe);
    assert(command_queue->items.size()==1 && g_command_queue->items.empty());
    assert(trace_rx==6 && g_uart_queue->items.empty());
    PendingCommand checked{}; xQueueReceive(command_queue,&checked,0);
    assert(checked.kind==3 && checked.request_id==101 && !strcmp(checked.value,"BLE_UART"));

    // Nonzero ordinary BLE mode startup preserves its ID in the existing
    // command queue; the zero-ID form cannot enter this correlated route.
    run(frame("sys/mode","BLE",111));
    assert(command_queue->items.size()==1 && g_command_queue->items.empty());
    xQueueReceive(command_queue,&checked,0);
    assert(checked.kind==2 && checked.request_id==111 && !strcmp(checked.value,"BLE"));
    run(frame("sys/mode","BLE",0));
    assert(command_queue->items.empty() && g_command_queue->items.empty());

    // Pair confirmation stays on the AmebaServices command queue, not the
    // legacy sys/comm companion RadioCommand path.
    run(frame("sys/bw/pair","CONFIRM:23",112));
    assert(command_queue->items.size()==1 && g_command_queue->items.empty());
    xQueueReceive(command_queue,&checked,0);
    assert(checked.kind==4 && checked.request_id==112 && !strcmp(checked.value,"CONFIRM:23"));
    run(frame("sys/bw/pair","CANCEL:23",0));
    assert(command_queue->items.empty() && g_command_queue->items.empty());

    // Old pre-BLE PINGs (including their nonzero IDs) retain legacy dispatch.
    auto legacy=frame("sys/bw/ping","PING",102); run(legacy);
    xQueueReceive(command_queue,&checked,0);
    assert(checked.kind==3 && checked.request_id==0 && !strcmp(checked.value,""));
    auto uncorrelated=frame("sys/bw/ping","BLE_UART",0); run(uncorrelated);
    xQueueReceive(command_queue,&checked,0);
    assert(checked.request_id==0 && !strcmp(checked.value,""));

    // Saturation never spills a diagnostic into the radio-control queue.
    for (int i=0;i<8;i++) assert(AmebaServices::enqueue(3,"",0));
    run(frame("sys/bw/ping","BLE_UART",103));
    assert(command_queue->items.size()==8 && g_command_queue->items.empty());
    assert(g_uart_queue->items.empty() && trace_rx==6);
    // Business replies must survive UART -> BLE, including the new RAM script topic.
    run(frame("app/usb","result=snapshot;state=empty",120));
    assert(notification_queue->items.size()==1);
    PendingNotification usb_reply{};
    xQueueReceive(notification_queue,&usb_reply,0);
    assert(usb_reply.request_id==120 && !strcmp(usb_reply.topic,"app/usb"));
    assert(!strcmp(usb_reply.value,"result=snapshot;state=empty"));
    run(frame("audit/put","stored:1",121));
    assert(AuditRelay::received==1 && AuditRelay::lastRequest==121);
    assert(notification_queue->items.empty()); // local ACK is not a BLE notification
    run(frame("audit/read","id=1;size=40;pending=1;dropped=0",122));
    PendingNotification audit_reply{};
    assert(xQueueReceive(notification_queue,&audit_reply,0));
    assert(audit_reply.request_id==122 && !strcmp(audit_reply.topic,"audit/read"));
    run(frame("audit/ack","ok",123));
    assert(xQueueReceive(notification_queue,&audit_reply,0));
    assert(audit_reply.request_id==123 && !strcmp(audit_reply.value,"ok"));
    return 0;
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-uart-ingress-") as folder:
            source = Path(folder) / "ingress.cpp"
            binary = Path(folder) / "ingress.exe"
            source.write_text(production, encoding="utf-8")
            result = subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "firmware/shared"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run_result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run_result.returncode, 0, run_result.stdout + run_result.stderr)


if __name__ == "__main__":
    unittest.main()
