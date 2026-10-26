"""Real wire decoder -> production sensor callback -> input pump -> poll."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from tools.tests.test_bw16_uart_ingress import extract_function

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware/rtl8720dn/NanoUI"


def no_includes(text):
    return re.sub(r"^#include[^\n]*", "", text, flags=re.M)


class ExclusiveInputTests(unittest.TestCase):
    def test_wire_input_works_without_main_service_or_radio(self):
        services = (FW / "AmebaServices.h").read_text(encoding="utf-8")
        subscription = extract_function(services, 'AmebaEventBus::subscribe("sns/all"') + ");"
        source = (
            r"""
#include "crabit_wire.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <stdexcept>
using String=std::string;
struct Queue {unsigned count,size;std::deque<std::vector<uint8_t>> items;};
using QueueHandle_t=Queue*;
constexpr int pdTRUE=1;
QueueHandle_t xQueueCreate(unsigned n,unsigned s){return new Queue{n,s,{}};}
int xQueueSend(Queue* q,const void* p,int){if(q->items.size()==q->count)return 0;const auto* b=(const uint8_t*)p;q->items.emplace_back(b,b+q->size);return 1;}
int xQueueReceive(Queue* q,void* p,int){if(q->items.empty())return 0;memcpy(p,q->items.front().data(),q->size);q->items.pop_front();return 1;}
enum EventType {EVT_NONE,EVT_UP,EVT_DOWN,EVT_LEFT,EVT_RIGHT,EVT_OK,EVT_BACK};
struct UIEvent{EventType type;int value;};
struct InputDriver{virtual void init()=0;virtual UIEvent poll()=0;};
"""
            + no_includes((FW / "AmebaStore.h").read_text(encoding="utf-8"))
            + r"""
float AmebaStore::imu_ax=0,AmebaStore::imu_ay=0,AmebaStore::imu_az=0;
int AmebaStore::opt_dx=0,AmebaStore::opt_dy=0,AmebaStore::battery_pct=0,AmebaStore::current_ui_event=0;
bool AmebaStore::opt_lifted=false;uint8_t AmebaStore::raw_btn_mask=0;
QueueHandle_t AmebaStore::ui_event_queue=nullptr;
"""
            + no_includes((FW / "AmebaEventBus.h").read_text(encoding="utf-8"))
            + r"""
std::vector<Subscription> AmebaEventBus::subs;
int radioCalls=0,controlCalls=0;
namespace RadioSvc{void traceCompanionReceived(uint16_t){}void service(){++radioCalls;}}
namespace AuditRelay{void receive(const CrabitWire::Message&) {}}
class AmebaServices{public:
"""
            + extract_function(services, "struct PendingSensors")
            + r""";
inline static QueueHandle_t sensor_queue=nullptr;
"""
            + extract_function(services, "static void applySensors")
            + "\n"
            + extract_function(services, "static void serviceInput")
            + r"""
static void initialize(){sensor_queue=xQueueCreate(8,sizeof(PendingSensors));
"""
            + subscription
            + r"""}
template<class... T>static void enqueueNotification(T...){++controlCalls;}
template<class... T>static void enqueueMode(T...){++controlCalls;}
template<class... T>static void enqueueModePair(T...){++controlCalls;}
template<class... T>static void enqueueCompanion(T...){++controlCalls;}
template<class... T>static void enqueueUartProbe(T...){++controlCalls;}
};
"""
            + no_includes((FW / "src/drivers/uart_input.h").read_text(encoding="utf-8"))
            + r"""
struct SerialPort{std::deque<uint8_t> bytes;int available(){return bytes.size();}int read(){int x=bytes.front();bytes.pop_front();return x;}}Serial;
unsigned uart_trace_bytes=0,uart_trace_frames=0,uart_trace_bad=0;
struct Done{};
void vTaskDelay(unsigned){throw Done{};}
unsigned pdMS_TO_TICKS(unsigned n){return n;}
"""
            + extract_function(
                (FW / "src/main_entry.cpp").read_text(encoding="utf-8"), "void Task_UART_Worker("
            )
            + r"""
void feed(const char* topic,const char* value){
 uint8_t payload[CrabitWire::PAYLOAD_SIZE],frame[CrabitWire::FRAME_SIZE];size_t n=0,m=0;
 assert(CrabitWire::encode_message(topic,value,0,payload,sizeof(payload),&n));
 assert(CrabitWire::encode_frame(payload,n,frame,sizeof(frame),&m));
 Serial.bytes.assign(frame,frame+m);try{Task_UART_Worker(nullptr);}catch(Done&){}
}
int main(){
 AmebaStore::initRuntimeQueues();AmebaServices::initialize();UartInputDriver input;
 feed("sns/all","100,200,300|1,2,0|70|16");assert(AmebaStore::raw_btn_mask==0);
 assert(input.poll().type==EVT_OK);assert(AmebaStore::raw_btn_mask==16);
 assert(AmebaStore::battery_pct==70 && AmebaStore::imu_ax==1.0f);
 assert(input.poll().type==EVT_NONE);
 feed("sns/all","0,0,0|0,0,0|70|16");assert(input.poll().type==EVT_NONE);
 feed("sns/all","0,0,0|0,0,0|70|0");assert(input.poll().type==EVT_NONE);assert(AmebaStore::raw_btn_mask==0);
 feed("sns/all","0,0,0|0,0,0|70|2");assert(input.poll().type==EVT_DOWN);
 feed("sns/all","0,0,0|0,0,0|70|0");feed("sns/all","0,0,0|0,0,0|70|32");assert(input.poll().type==EVT_BACK);
 feed("sns/all","bad input");assert(input.poll().type==EVT_NONE);
 assert(radioCalls==0 && controlCalls==0 && uart_trace_bad==0);
 // Multiple samples queued while an App owns the main loop preserve event order.
 feed("sns/all","0,0,0|0,0,0|70|0");feed("sns/all","0,0,0|0,0,0|70|1");
 feed("sns/all","0,0,0|0,0,0|70|0");feed("sns/all","0,0,0|0,0,0|70|16");
 assert(input.poll().type==EVT_UP);assert(input.poll().type==EVT_OK);assert(input.poll().type==EVT_NONE);
}
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            cpp, exe = Path(directory) / "input.cpp", Path(directory) / "input.exe"
            cpp.write_text(source, encoding="utf-8")
            result = subprocess.run(
                [
                    shutil.which("g++"),
                    "-std=c++17",
                    "-I",
                    str(ROOT / "firmware/shared"),
                    str(cpp),
                    "-o",
                    str(exe),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)
