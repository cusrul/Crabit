"""Compile the unchanged-source EventBus header with mock queues/snapshots.

This checks dispatch/formatting, not real FreeRTOS scheduling or sensor hardware.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Stm32DiagnosticContracts(unittest.TestCase):
    def test_real_event_bus_budget_snapshot_age_and_wire_limits(self):
        compiler = next(
            (
                p
                for p in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++"))
                if p and Path(p).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "existing local g++ required")
        with tempfile.TemporaryDirectory(prefix="crabit-diag-host-") as temporary:
            temp = Path(temporary)
            inc = temp / "firmware/stm32/Core/Inc"
            inc.mkdir(parents=True)
            shared = temp / "firmware/shared"
            shared.mkdir(parents=True)
            shutil.copyfile(ROOT / "firmware/stm32/Core/Inc/event_bus.h", inc / "event_bus.h")
            shutil.copyfile(ROOT / "firmware/stm32/Core/Inc/sys_config.h", inc / "sys_config.h")
            shutil.copyfile(
                ROOT / "firmware/stm32/Core/Inc/ameba_session.h", inc / "ameba_session.h"
            )
            shutil.copyfile(ROOT / "firmware/shared/crabit_wire.h", shared / "crabit_wire.h")
            for name in ("audit_contract.h", "audit_spool.h"):
                shutil.copyfile(ROOT / "firmware/shared" / name, shared / name)
            shutil.copyfile(ROOT / "firmware/shared/imu_telemetry.h", shared / "imu_telemetry.h")
            shutil.copyfile(
                ROOT / "firmware/shared/product_preferences.h", shared / "product_preferences.h"
            )
            shutil.copyfile(ROOT / "firmware/shared/watch_face.h", shared / "watch_face.h")
            shutil.copyfile(
                ROOT / "firmware/shared/settings_contract.h", shared / "settings_contract.h"
            )
            self.assertEqual(
                (inc / "event_bus.h").read_bytes(),
                (ROOT / "firmware/stm32/Core/Inc/event_bus.h").read_bytes(),
            )
            # Fixture-only headers and program; no production-source rewriting.
            (inc / "usb_script.h").write_text(
                """
#pragma once
#include <stddef.h>
#include <cassert>
inline void CrabitUsb_ScriptHandle(const char*, size_t, char*, size_t) {
    assert(false && "diagnostic tests must not dispatch USB scripts");
}
""",
                encoding="utf-8",
            )
            (inc / "usart.h").write_text(
                """
#pragma once
#include <stdint.h>
extern uint32_t fake_tick;
inline uint32_t HAL_GetTick() { return fake_tick; }
class WindowManager { public: static WindowManager* instance; void markDirty() {} };
""",
                encoding="utf-8",
            )
            (inc / "global_store.h").write_text(
                "#pragma once\n#include <stdint.h>\n"
                "struct GlobalDataStore { uint8_t sys_volume = 100; };\n"
                "extern GlobalDataStore GlobalStore;\n",
                encoding="utf-8",
            )
            (inc / "main.h").write_text(
                "#pragma once\n#ifndef __MAIN_H\n#define __MAIN_H\n#endif\n", encoding="utf-8"
            )
            (inc / "sensor_snapshot.h").write_text(
                """
#pragma once
struct StmSensorSnapshot {
    uint32_t published_at;
    struct { float ax, ay, az; bool is_valid; uint32_t timestamp; } imu;
    struct { int dx, dy; bool lifted; } optical;
    struct { float pressure, temperature; } pressure;
    struct { float voltage; int percent; bool is_valid; bool is_charging; uint32_t timestamp; } battery;
};
extern StmSensorSnapshot fake_snapshot;
extern bool readable;
inline bool STM32_ReadSensorSnapshot(StmSensorSnapshot* out) {
    if (!readable) return false;
    *out = fake_snapshot; return true;
}
""",
                encoding="utf-8",
            )
            (inc / "FreeRTOS.h").write_text(
                """
#pragma once
using BaseType_t = int;
constexpr int pdTRUE = 1, pdFALSE = 0, pdPASS = 1;
#define pdMS_TO_TICKS(x) (x)
#define portYIELD_FROM_ISR(x) ((void)(x))
inline unsigned __get_IPSR() { return 0; }
""",
                encoding="utf-8",
            )
            (inc / "queue.h").write_text(
                """
#pragma once
#include <deque>
#include <vector>
#include <cstring>
struct FakeQueue { size_t capacity, size; std::deque<std::vector<char>> items; };
using QueueHandle_t = FakeQueue*;
extern std::vector<QueueHandle_t> queues;
inline QueueHandle_t xQueueCreate(size_t capacity, size_t size) {
    auto* queue = new FakeQueue{capacity, size, {}}; queues.push_back(queue); return queue;
}
inline int xQueueSend(QueueHandle_t queue, const void* value, int) {
    if (queue->items.size() == queue->capacity) return 0;
    queue->items.emplace_back((const char*)value, (const char*)value + queue->size); return 1;
}
inline int xQueueSendFromISR(QueueHandle_t q, const void* v, BaseType_t*) { return xQueueSend(q, v, 0); }
inline int xQueueReceive(QueueHandle_t queue, void* value, int) {
    if (queue->items.empty()) return 0;
    std::memcpy(value, queue->items.front().data(), queue->size); queue->items.pop_front(); return 1;
}
""",
                encoding="utf-8",
            )
            source = temp / "check.cpp"
            source.write_text(
                r"""
#include "event_bus.h"
#include "sys_config.h"
#include <cassert>
#include <string>
#include <iostream>
#include <limits>
uint32_t fake_tick = 1000;
WindowManager* WindowManager::instance = nullptr;
std::vector<QueueHandle_t> queues;
StmSensorSnapshot fake_snapshot{};
bool readable = true;
int legacy_script_writes = 0;
void legacyScript(const char*) { ++legacy_script_writes; }
SystemConfig sys_cfg;
GlobalDataStore GlobalStore;
std::atomic<Crabit::AmebaPhase> Crabit::ameba_phase(Crabit::AmebaPhase::Inactive);
volatile bool g_coproc_active = false;
unsigned settings_flat_flash_unlock_calls = 0, settings_flat_flash_erase_calls = 0;
unsigned settings_flat_flash_program_calls = 0, settings_flat_flash_lock_calls = 0;
unsigned settings_flat_program_fail_call = 0;
uint8_t settings_flat_flash_image[56] = {};
int settings_flat_unlock_status = HAL_OK, settings_flat_lock_status = HAL_OK;
int settings_flat_erase_status = HAL_OK, settings_flat_program_status = HAL_OK;
static_assert(sizeof(SystemConfig) == 48, "legacy config footprint must remain unchanged");
static_assert(sizeof(settings_flat_flash_image) == 56, "fake flash includes the 8-byte trailer");
unsigned oled_calls = 0;
unsigned badusb_dispatches = 0;
extern "C" void OLED_WR_Byte(uint8_t, uint8_t) { ++oled_calls; }
void USB_LOG(const char*, ...) {}
void count_badusb(const char*) { ++badusb_dispatches; }

void enqueue(const char* topic, const char* value, uint16_t id) {
    uint8_t payload[CrabitWire::PAYLOAD_SIZE]; size_t size;
    CrabitWire::Message message{};
    assert(CrabitWire::encode_message(topic, value, id, payload, sizeof(payload), &size));
    assert(CrabitWire::decode_message(payload, size, &message));
    assert(EventBus::enqueueMessage(message));
}
CrabitWire::Message pop() {
    CrabitWire::Message message{};
    assert(xQueueReceive(EventBus::tx_queue, &message, 0) == pdPASS);
    return message;
}
void ack(uint16_t id) {
    auto message = pop();
    assert(std::string(message.topic) == "ack" && message.request_id == id);
}
std::string snapshot(uint16_t id) {
    enqueue("diag/snapshot", "read", id);
    EventBus::processPending(1);
    auto message = pop(); ack(id);
    assert(std::string(message.topic) == "diag/snapshot" && message.request_id == id);
    assert(message.value_len <= CrabitWire::MAX_VALUE);
    return message.value;
}
std::string businessReply(const char* topic, const char* payload, uint16_t id) {
    enqueue(topic, payload, id);
    EventBus::processPending(1);
    const auto message = pop();
    assert(std::string(message.topic) == topic && message.request_id == id);
    assert(message.value_len <= CrabitWire::MAX_VALUE);
    assert(EventBus::tx_queue->items.empty()); // business result is the correlated same-topic reply
    return message.value;
}
int main() {
    assert(EventBus::init());
    assert(EventBus::subscribe("app/badusb", legacyScript));
    enqueue("app/badusb", "0|STRING test", 11); EventBus::processPending(1);
    auto rejected = pop();
    assert(std::string(rejected.topic) == "app/badusb");
    assert(std::string(rejected.value) == "UNSUPPORTED" && rejected.request_id == 11);
    assert(legacy_script_writes == 0 && EventBus::tx_queue->items.empty());
    assert(EventBus::subscribe("app/badusb", count_badusb));
    enqueue("diag/echo", "first", 1); enqueue("diag/echo", "second", 2);
    EventBus::processPending(0);
    assert(EventBus::tx_queue->items.empty());
    EventBus::processPending(1);
    assert(std::string(pop().value) == "first"); ack(1);
    assert(EventBus::tx_queue->items.empty());
    EventBus::processPending();
    assert(std::string(pop().value) == "second"); ack(2);
    fake_snapshot.published_at = 123;
    auto old = snapshot(3);
    assert(old.find(";at=123;age=877") != std::string::npos);
    fake_tick += 10;
    assert(snapshot(4).find(";age=887") != std::string::npos);
    fake_snapshot.published_at = fake_tick;
    assert(snapshot(5).find(";age=0") != std::string::npos);
    fake_tick = 5; fake_snapshot.published_at = 0xfffffff0u;
    assert(snapshot(6).find(";age=21") != std::string::npos);
    fake_snapshot.imu.ax = fake_snapshot.imu.ay = fake_snapshot.imu.az = std::numeric_limits<float>::max();
    fake_snapshot.pressure.pressure = fake_snapshot.pressure.temperature = std::numeric_limits<float>::max();
    auto oversized = snapshot(7);
    assert(oversized.find("unavailable;at=") == 0 && oversized.find(";age=21") != std::string::npos);
    readable = false;
    assert(snapshot(8) == "unavailable");
    readable = true; fake_tick = 2000;
    fake_snapshot.imu = {0, 0, 1, true, 1980};
    enqueue("diag/imu", "", 9); EventBus::processPending(1);
    auto tilt = pop(); ack(9);
    assert(std::string(tilt.topic) == "diag/imu" && tilt.request_id == 9);
    assert(std::string(tilt.value).find("age=20;valid=1;mode=tilt") != std::string::npos);
    assert(tilt.value_len <= CrabitWire::MAX_VALUE);
    fake_tick = 4000;
    enqueue("diag/imu", "", 10); EventBus::processPending(1);
    assert(std::string(pop().value).find("valid=0") != std::string::npos); ack(10);

    fake_tick = 1000;
    fake_snapshot.battery = {3.85f, 88, true, true, 990};
    auto battery = businessReply("diag/battery", "read", 14);
    assert(battery == "mv=3850;pct=88;charging=1;valid=1;at=990;age=10");
    assert(businessReply("diag/battery", "status", 15) == "invalid");
    fake_tick = 2000; fake_snapshot.battery.timestamp = 123;
    battery = businessReply("diag/battery", "read", 16);
    assert(battery == "mv=3850;pct=88;charging=1;valid=1;at=123;age=1877");
    fake_tick = 5; fake_snapshot.battery.timestamp = 0xfffffff0u;
    battery = businessReply("diag/battery", "read", 17);
    assert(battery == "mv=3850;pct=88;charging=1;valid=1;at=4294967280;age=21");
    fake_snapshot.battery.is_valid = false;
    battery = businessReply("diag/battery", "read", 18);
    assert(battery == "mv=0;pct=0;charging=1;valid=0;at=4294967280;age=21");
    fake_snapshot.battery.is_valid = true;

    // Real settings read/set handlers return a correlated business response.
    readable = true;
    sys_cfg.brightness = .25f; sys_cfg.invert_disp = false;
    sys_cfg.screen_off_ms = 3000; sys_cfg.deep_sleep_ms = 10000;
    sys_cfg.kill_app_ms = 5000; sys_cfg.enable_ble = false;

    // The optional preferences trailer follows the unchanged 48-byte config.
    CrabitPreferences::load(CrabitPreferences::Record{});
    assert(CrabitPreferences::enabled(CrabitPreferences::Pat) &&
           CrabitPreferences::enabled(CrabitPreferences::Qmi) &&
           CrabitPreferences::enabled(CrabitPreferences::Sound) &&
           CrabitPreferences::enabled(CrabitPreferences::Led) &&
           CrabitPreferences::current.volume == 100 && CrabitPreferences::current.led_level == 8);
    SystemConfig legacy_before_preferences = sys_cfg;
    auto prefs = businessReply("sys/prefs", "read", 30);
    assert(prefs.find("result=snapshot;error=none;pat=1;qmi=1;") == 0);
    assert(prefs.find("sound=1;led=1;volume=100;led_level=8;") != std::string::npos);
    prefs = businessReply("sys/prefs/set", "pat=0;qmi=0;sound=0;led=0;volume=42;led_level=3", 31);
    assert(prefs.find("result=saved;error=none;pat=0;qmi=0;") == 0);
    assert(prefs.find("sound=0;led=0;volume=42;led_level=3;") != std::string::npos);
    assert(GlobalStore.sys_volume == 42);
    assert(std::memcmp(settings_flat_flash_image, &legacy_before_preferences,
                       sizeof(SystemConfig)) == 0);
    CrabitPreferences::Record saved_preferences{};
    std::memcpy(&saved_preferences, settings_flat_flash_image + sizeof(SystemConfig),
                sizeof(saved_preferences));
    assert(CrabitPreferences::valid(saved_preferences));
    assert(saved_preferences.volume == 42 && saved_preferences.led_level == 3 &&
           !(saved_preferences.flags & CrabitPreferences::Pat) &&
           !(saved_preferences.flags & CrabitPreferences::Qmi) &&
           !(saved_preferences.flags & CrabitPreferences::Sound) &&
           !(saved_preferences.flags & CrabitPreferences::Led));

    enqueue("sys/settings", "read", 20); EventBus::processPending(1);
    auto settings = pop();
    assert(std::string(settings.topic) == "sys/settings" && settings.request_id == 20);
    assert(std::string(settings.value).find("result=snapshot;error=none;display=unchanged;") == 0);

    g_coproc_active = true;
    Crabit::setAmebaPhase(Crabit::AmebaPhase::Running);
    const auto oled_before = oled_calls;
    enqueue("sys/settings/set", "brightness=800;invert=1;deep_sleep_ms=0", 21);
    EventBus::processPending(1);
    settings = pop();
    assert(std::string(settings.value).find("result=saved;error=none;display=deferred;") == 0);
    assert(settings.request_id == 21 && sys_cfg.brightness > .799f && sys_cfg.invert_disp);
    assert(sys_cfg.screen_off_ms == 3000 && sys_cfg.kill_app_ms == 5000 && !sys_cfg.enable_ble);
    assert(oled_calls == oled_before);

    const auto unlocks_before_transition = settings_flat_flash_unlock_calls;
    Crabit::setAmebaPhase(Crabit::AmebaPhase::Transition);
    enqueue("sys/settings/set", "invert=0", 22); EventBus::processPending(1);
    settings = pop();
    assert(std::string(settings.value).find("result=busy;error=owner;") == 0);
    assert(settings_flat_flash_unlock_calls == unlocks_before_transition);
    prefs = businessReply("sys/prefs/set", "pat=1", 32);
    assert(prefs.find("result=busy;error=owner;") == 0);
    assert(settings_flat_flash_unlock_calls == unlocks_before_transition);
    Crabit::setAmebaPhase(Crabit::AmebaPhase::Running);

    const auto writes_before_invalid = settings_flat_flash_unlock_calls;
    const auto flags_before_invalid = CrabitPreferences::current.flags;
    prefs = businessReply("sys/prefs/set", "pat=1;pat=0", 33);
    assert(prefs.find("result=invalid;error=payload;") == 0);
    assert(CrabitPreferences::current.flags == flags_before_invalid &&
           settings_flat_flash_unlock_calls == writes_before_invalid);
    enqueue("sys/settings/set", "always_on=1", 27); EventBus::processPending(1);
    settings = pop();
    assert(std::string(settings.value).find("result=invalid;error=payload;") == 0);
    assert(settings_flat_flash_unlock_calls == writes_before_invalid);

    enqueue("app/badusb", "1000|not-a-script", 28); EventBus::processPending(1);
    settings = pop();
    assert(std::string(settings.topic) == "app/badusb" &&
           std::string(settings.value) == "UNSUPPORTED" && settings.request_id == 28);
    assert(badusb_dispatches == 0);

    settings_flat_unlock_status = HAL_ERROR;
    enqueue("sys/settings/set", "screen_off_ms=5000", 23); EventBus::processPending(1);
    settings = pop();
    assert(std::string(settings.value).find("result=save_failed;error=unlock;") == 0);
    settings_flat_unlock_status = HAL_OK;
    settings_flat_erase_status = HAL_ERROR;
    enqueue("sys/settings/set", "screen_off_ms=5000", 24); EventBus::processPending(1);
    settings = pop();
    assert(std::string(settings.value).find("result=save_failed;error=erase;") == 0);
    settings_flat_erase_status = HAL_OK;
    settings_flat_program_status = HAL_ERROR;
    enqueue("sys/settings/set", "screen_off_ms=5000", 25); EventBus::processPending(1);
    settings = pop();
    assert(std::string(settings.value).find("result=save_failed;error=program;") == 0);
    settings_flat_program_status = HAL_OK;
    settings_flat_lock_status = HAL_ERROR;
    enqueue("sys/settings/set", "screen_off_ms=5000", 26); EventBus::processPending(1);
    settings = pop();
    assert(std::string(settings.value).find("result=save_failed;error=lock;") == 0);
    for (auto* queue : queues) delete queue;
    std::cout << "actual EventBus snapshot/battery/settings/preferences correlation, ownership, HAL failures and wire checks passed\n";
}
""",
                encoding="utf-8",
            )
            binary = temp / "check.exe"
            build = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-include",
                    str(ROOT / "tools/tests/settings_flat_fakes/fake_hal.h"),
                    "-I",
                    str(inc),
                    str(source),
                    "-o",
                    str(binary),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn(
                "actual EventBus snapshot/battery/settings/preferences correlation, ownership, HAL failures and wire checks passed",
                run.stdout,
            )


if __name__ == "__main__":
    unittest.main()
