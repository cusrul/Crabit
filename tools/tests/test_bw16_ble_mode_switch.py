"""Run the real AppBleSpam logic against fake radio/input and library APIs."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/rtl8720dn/NanoUI/app/app_ble_spam.h"
FAKE = r'''
#include <cstdint>
#include <cassert>
#include <vector>
enum EventType { EVT_NONE, EVT_UP, EVT_DOWN, EVT_BACK };
struct UIEvent { EventType type; };
struct InputDriver { std::vector<EventType> events; unsigned pos=0; UIEvent poll() { return {events.at(pos++)}; } };
struct Canvas { void clear() {} void display() {} void drawText(int,int,const char*,int) {} };
enum AppMode { APP_MODE_EXCLUSIVE };
struct Application { virtual ~Application() = default; virtual const char* getAppName() = 0; virtual AppMode getMode() = 0; virtual void runExclusive(Canvas*, InputDriver*) = 0; };
bool is_display_active = true;
unsigned long clock_ms = 1000;
unsigned long millis() { return clock_ms; }
void safe_delay(int n) { clock_ms += n; }
namespace RadioSvc {
 enum State { IDLE, BUSY }; struct Snapshot { State state; };
 bool occupied = false; Snapshot snapshot() { return {occupied ? BUSY : IDLE}; }
 bool companionActive() { return false; }
}
struct BW16BLESpammer {
 enum SpamType { ALL_BURST, ANDROID_BURST, IOS_NEW_DEVICE, IOS_ACTION_MODAL };
 static std::vector<SpamType> started; static int begins, stops, ticks;
 void begin() { begins++; } void stop() { stops++; } void tick() { ticks++; }
 void setDebugLogging(bool b) { assert(!b); } void setInterval(int n) { assert(n==1000); }
 void start(SpamType t) { started.push_back(t); }
};
std::vector<BW16BLESpammer::SpamType> BW16BLESpammer::started;
int BW16BLESpammer::begins=0; int BW16BLESpammer::stops=0; int BW16BLESpammer::ticks=0;
'''
MAIN = r'''
int main() {
 Canvas canvas; AppBleSpam app;
 InputDriver input{{EVT_DOWN,EVT_DOWN,EVT_DOWN,EVT_DOWN,EVT_UP,EVT_BACK}};
 app.runExclusive(&canvas,&input);
 using S=BW16BLESpammer;
 assert((S::started==std::vector<S::SpamType>{S::ALL_BURST,S::ANDROID_BURST,S::IOS_NEW_DEVICE,S::IOS_ACTION_MODAL,S::ALL_BURST,S::IOS_ACTION_MODAL}));
 assert(S::begins==1 && S::stops==1 && S::ticks==5);
 S::started.clear(); RadioSvc::occupied=true;
 InputDriver busy{{EVT_DOWN, EVT_BACK}}; app.runExclusive(&canvas,&busy);
 assert(S::started.empty() && S::begins==1 && S::stops==1);
}
'''


class BleModeSwitch(unittest.TestCase):
    def test_modes_wrap_and_busy_does_not_touch_radio(self):
        compiler = shutil.which("g++") or r"C:\w64dev\w64devkit\bin\g++.exe"
        text = SOURCE.read_text(encoding="utf-8")
        for include in ('#include "../src/NanoUI.h"', '#include "../RadioSvc.h"', '#include <BW16BLESpam.h>'):
            self.assertEqual(text.count(include), 1)
            text = text.replace(include, "")
        with tempfile.TemporaryDirectory(prefix="crabit-ble-switch-") as temp:
            cpp = Path(temp) / "test.cpp"
            exe = Path(temp) / "test.exe"
            cpp.write_text(FAKE + text + MAIN, encoding="utf-8")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
