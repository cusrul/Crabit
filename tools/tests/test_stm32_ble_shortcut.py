"""Run the actual STM32 input driver against physical-pin and time doubles."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tools.tests.test_bw16_uart_ingress import extract_function

ROOT = Path(__file__).resolve().parents[2]


class BleShortcutInputTests(unittest.TestCase):
    def test_actual_input_short_long_chord_and_startup_hold(self):
        source = (ROOT / "firmware/stm32/Core/Inc/hw_input.h").read_text(encoding="utf-8")
        program = (
            r"""
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include "idle_sleep_policy.h"
#include "ble_shortcut_button.h"
#include "product_preferences.h"
enum EventType {EVT_NONE,EVT_UP,EVT_DOWN,EVT_LEFT,EVT_RIGHT,EVT_OK,EVT_BACK};
struct UIEvent {EventType type; int value;};
class InputDriver {public: virtual void init()=0; virtual UIEvent poll()=0;};
class InputFeedback {
public:
 static unsigned presses,ticks;
 static void press(){++presses;}
 static void tick(){++ticks;}
};
unsigned InputFeedback::presses=0;
unsigned InputFeedback::ticks=0;
enum {GPIOA=1,GPIOH=2,GPIO_PIN_7=7,GPIO_PIN_6=6,GPIO_PIN_3=3,GPIO_PIN_RESET=0};
bool up=false,down=false,ok=false;
uint32_t now=1000;
uint32_t HAL_GetTick(){return now;}
uint8_t HAL_GPIO_ReadPin(int port,int pin){
 if(port==GPIOA && pin==GPIO_PIN_7)return !up;
 if(port==GPIOA && pin==GPIO_PIN_6)return !down;
 assert(port==GPIOH && pin==GPIO_PIN_3);return ok;
}
namespace SysPower {void feed(){}}
namespace SysAudio {void play(int,int){}}
struct {int sys_volume=0;} GlobalStore;
struct StmSensorSnapshot {
 struct {bool is_valid=false;float ax=0,ay=0,az=0;} imu;
 struct {bool is_valid=false,lifted=true;int dx=0,dy=0;} optical;
} snapshot;
bool STM32_ReadSensorSnapshot(StmSensorSnapshot* out){*out=snapshot;return true;}
namespace Crabit {unsigned toggles=0;void toggleBleFromUi(){++toggles;}}
"""
            + extract_function(source, "class STM32Input : public InputDriver")
            + r""";
int main(){
 STM32Input input;input.init();
 up=true;now=1001;assert(input.poll().type==EVT_NONE);
 now=2000;assert(input.poll().type==EVT_NONE && Crabit::toggles==0);
 now=2001;assert(input.poll().type==EVT_NONE && Crabit::toggles==1);
 now=3200;assert(input.poll().type==EVT_NONE && Crabit::toggles==1);
 up=false;now=3500;assert(input.poll().type==EVT_NONE);
 up=true;now=4000;assert(input.poll().type==EVT_NONE);
 up=false;now=4040;assert(input.poll().type==EVT_UP);
 now=4050;assert(input.poll().type==EVT_NONE);
 up=true;now=4300;input.poll();up=false;now=4310;assert(input.poll().type==EVT_NONE);
 up=down=true;now=5000;input.poll();now=6100;input.poll();
 down=false;now=6200;input.poll();up=false;now=6300;input.poll();assert(Crabit::toggles==1);
 // Boot/wake with a key held is disarmed until a complete release.
 up=true;input.init();now=8000;input.poll();assert(Crabit::toggles==1);
 up=false;now=8100;input.poll();up=true;now=8200;input.poll();
 now=9200;input.poll();assert(Crabit::toggles==2);
 up=false;now=9300;input.poll();
 // Early IMU events cannot starve the physical long-hold recognizer.
 snapshot.imu.is_valid=true;snapshot.imu.ay=0.8f;snapshot.imu.az=1;
 up=true;now=10000;input.poll();now=11000;input.poll();assert(Crabit::toggles==3);
 up=false;now=11100;input.poll();snapshot.imu.is_valid=false;
 // A key held through STOP2 wake is suppressed until a release is observed.
 const unsigned toggles_before_wake=Crabit::toggles;
 up=true;now=11200;input.resetAfterWake();
 now=11300;assert(input.poll().type==EVT_NONE && Crabit::toggles==toggles_before_wake);
 up=false;now=11399;assert(input.poll().type==EVT_NONE);
 now=11400;assert(input.poll().type==EVT_NONE);
 now=11401;assert(input.poll().type==EVT_NONE);
 up=true;now=11500;input.poll();now=12500;assert(input.poll().type==EVT_NONE);
 assert(Crabit::toggles==toggles_before_wake+1);
 up=false;now=12600;input.poll();
 // Existing right OK/BACK multiplexing remains unchanged.
 ok=true;now=12000;input.poll();ok=false;now=12100;assert(input.poll().type==EVT_OK);
 ok=true;now=13000;input.poll();now=13551;assert(input.poll().type==EVT_BACK);
 ok=false;now=13600;assert(input.poll().type==EVT_NONE);
 // Tick wrap remains a one-shot, and no UP is emitted after a long hold.
 Crabit::BleShortcutButton gesture;gesture.reset(false);
 assert(gesture.update(true,false,UINT32_MAX-499)==Crabit::BleShortcutButton::None);
 assert(gesture.update(true,false,500)==Crabit::BleShortcutButton::ToggleBle);
 assert(gesture.update(false,false,600)==Crabit::BleShortcutButton::None);
  // Physical controls remain available when both sensor-input preferences are off.
  CrabitPreferences::current.flags=static_cast<uint8_t>(CrabitPreferences::Sound|CrabitPreferences::Led);
  CrabitPreferences::current.volume=0;
  snapshot.imu.is_valid=false;snapshot.optical.is_valid=false;
  up=down=ok=false;now=15000;input.init();
  const unsigned feedback_before=InputFeedback::presses;
  up=true;now=15001;assert(input.poll().type==EVT_NONE);
  up=false;now=15041;assert(input.poll().type==EVT_UP);
  down=true;now=15300;assert(input.poll().type==EVT_DOWN);
  down=false;now=15340;assert(input.poll().type==EVT_NONE);
  ok=true;now=15600;assert(input.poll().type==EVT_NONE);
  ok=false;now=15640;assert(input.poll().type==EVT_OK);
  assert(!CrabitPreferences::enabled(CrabitPreferences::Pat));
  assert(!CrabitPreferences::enabled(CrabitPreferences::Qmi));
  assert(InputFeedback::presses==feedback_before+3);
}
"""
        )
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="crabit-ble-shortcut-") as folder:
            cpp = Path(folder) / "input.cpp"
            binary = Path(folder) / "input.exe"
            cpp.write_text(program, encoding="utf-8")
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "firmware/stm32/Core/Inc"),
                    "-I",
                    str(ROOT / "firmware/shared"),
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
