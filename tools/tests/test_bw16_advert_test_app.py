"""Compile the actual App against spies; no radio/device operation."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class AdvertAppTests(unittest.TestCase):
    def test_default_stop_busy_start_stop_exit_and_timeout(self):
        app = (ROOT / "firmware/rtl8720dn/NanoUI/app/app_ble_advert_test.h").read_text()
        app = re.sub(r"^#include[^\n]*", "", app, flags=re.M)
        fixture = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
enum {EVT_NONE,EVT_BACK,EVT_OK,EVT_UP,EVT_DOWN};
enum AppMode {APP_MODE_EXCLUSIVE};
struct UIEvent {int type;};
bool is_display_active=true;
uint32_t ticks=0, step=20;
uint32_t hal_get_ticks(){return ticks;}
void safe_delay(int){ticks+=step;}
struct Canvas {void clear(){} void drawText(int,int,const char*,int){} void display(){}};
struct InputDriver {std::vector<int> events;unsigned i=0;UIEvent poll(){return {i<events.size()?events[i++]:EVT_BACK};}};
struct Application {virtual const char* getAppName()=0;virtual AppMode getMode()=0;virtual void runExclusive(Canvas*,InputDriver*)=0;};
namespace RadioSvc {enum {IDLE,CONNECTED};struct Snapshot{int state;bool initialized;}; Snapshot state{IDLE,false};Snapshot snapshot(){return state;}bool companion=false;bool companionActive(){return companion;}}
enum {GAP_ADTYPE_ADV_NONCONN_IND,GAP_ADTYPE_FLAGS_GENERAL=1,GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED=2};
struct BLEAdvertData {void addFlags(int){}void addCompleteName(const char* name){assert(!strcmp(name,"Crabit-Test"));}};
struct Advert {int stops=0,starts=0,minimum=0;void stopAdv(){++stops;}void startAdv(){++starts;}void setAdvType(int){}void setAdvData(BLEAdvertData){}void setMinInterval(int ms){minimum=ms;}void setMaxInterval(int){}void updateAdvertParams(){}};
struct Device {int inits=0,ends=0;Advert advert;void init(){++inits;ticks+=100;}void deinit(){++ends;}void beginPeripheral(){++advert.starts;}Advert* configAdvert(){return &advert;}} BLE;
"""
            + app
            + r"""
void run(std::vector<int> events){Canvas canvas;InputDriver input{events};AppBleAdvertTest app;app.runExclusive(&canvas,&input);}
int main(){
 run({EVT_BACK});assert(BLE.inits==0 && BLE.ends==0);
 RadioSvc::state={RadioSvc::CONNECTED,true};run({EVT_OK,EVT_BACK});assert(BLE.inits==0);
 // initialized describes owner queues, not active radio ownership.
 RadioSvc::state={RadioSvc::IDLE,true};run({EVT_OK,EVT_BACK});assert(BLE.inits==1 && BLE.ends==1);
 BLE={};
 RadioSvc::state={RadioSvc::IDLE,false};RadioSvc::companion=true;run({EVT_OK,EVT_BACK});assert(BLE.inits==0);
 RadioSvc::companion=false;run({EVT_OK,EVT_OK,EVT_OK,EVT_BACK});
 assert(BLE.inits==1 && BLE.ends==1 && BLE.advert.starts==2 && BLE.advert.stops==2);
 BLE={};step=20000;ticks=0;run({EVT_DOWN,EVT_OK,EVT_NONE,EVT_NONE,EVT_NONE,EVT_BACK});
 assert(BLE.inits==1 && BLE.ends==1 && BLE.advert.starts==1 && BLE.advert.stops==2);
 assert(BLE.advert.minimum==2000);
 BLE={};step=20;ticks=UINT32_MAX-50;run({EVT_OK,EVT_BACK});assert(BLE.advert.stops==1);
}
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            cpp, exe = Path(directory) / "app.cpp", Path(directory) / "app.exe"
            cpp.write_text(fixture)
            built = subprocess.run(
                [shutil.which("g++"), "-std=c++17", str(cpp), "-o", str(exe)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)
