"""Compile and run the production PMIC driver plus battery publication semantics."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
AXP_HEADER = ROOT / "firmware/stm32/Core/Inc/axp2101.h"
AXP_SOURCE = ROOT / "firmware/stm32/Core/Src/axp2101.c"
AXP_C_TEST = ROOT / "tools/tests/axp2101_checked.c"
HW_SENSORS = ROOT / "firmware/stm32/Core/Inc/hw_sensors.h"
NANOUI_INCLUDE = ROOT / "firmware/stm32/Core/NanoUI/include"
FAKES = ROOT / "tools/tests/power_driver_fakes"

BATTERY_START = "class Battery_Driver : public NanoUI::Sensor {"
BATTERY_END = "// 3. PAT9125"
AXP_START = "class AXP2101_Driver : public NanoUI::Sensor {"
AXP_END = "// 4c. BMP280"

HARNESS_PRELUDE = """\
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "axp2101.h"
#include "NanoUI/core/sensor_types.h"

static uint32_t g_tick = 1000;
uint32_t HAL_GetTick() { return g_tick++; }

class I2cFault {
public:
    enum Device : uint32_t { Bus, Qmi, Battery, Pat, Bmp, Axp };
    static inline std::atomic<bool> paused{false};
    class Sample {
    public:
        Sample(Device, uint32_t) {}
    };
};

class AuxStop2 {
public:
    static inline std::atomic<bool> restoreFault{false};
};

namespace NanoUI {
class Sensor {
public:
    explicit Sensor(uint32_t) {}
    virtual ~Sensor() = default;
    virtual void init() = 0;
    virtual void update() = 0;
    virtual const char* getName() = 0;
    virtual const void* getData() = 0;
};
}  // namespace NanoUI

// Controllable stand-in for the PMIC read layer. The drivers under test are the
// real production classes; only the I2C-facing calls are replaced.
static bool voltage_ok = true, percent_ok = true, charging_ok = true;
static uint16_t voltage_mv = 3900;
static uint8_t percent_value = 78;
static bool charging_value = false;
static uint8_t chip_id = 0x4A;
static bool fuel_gauge_ok = true;
// Counts every PMU-facing call so driver init can be held to "no PMU access".
static unsigned pmu_calls = 0;

HAL_StatusTypeDef AXP2101_ReadReg(uint8_t reg, uint8_t* val) {
    ++pmu_calls;
    (void)reg;
    *val = chip_id;
    return HAL_OK;
}
HAL_StatusTypeDef AXP2101_Init(void) {
    ++pmu_calls;
    return HAL_OK;
}
bool AXP2101_InitFuelGauge(void) {
    ++pmu_calls;
    return fuel_gauge_ok;
}
bool AXP2101_TryGetBattVoltage(uint16_t* out) {
    ++pmu_calls;
    if (!voltage_ok) return false;
    *out = voltage_mv;
    return true;
}
bool AXP2101_TryGetBatteryPercent(uint8_t* out) {
    ++pmu_calls;
    if (!percent_ok) return false;
    *out = percent_value;
    return true;
}
bool AXP2101_TryGetCharging(bool* out) {
    ++pmu_calls;
    if (!charging_ok) return false;
    *out = charging_value;
    return true;
}
uint16_t AXP2101_GetBattVoltage(void) {
    ++pmu_calls;
    return voltage_ok ? voltage_mv : 0;
}
uint8_t AXP2101_GetBatteryPercent(void) {
    ++pmu_calls;
    return percent_ok ? percent_value : 0;
}
bool AXP2101_IsCharging(void) {
    ++pmu_calls;
    return charging_ok && charging_value;
}
bool AXP2101_IsVbusIn(void) {
    ++pmu_calls;
    return false;
}
float AXP2101_GetInternalTemp(void) {
    ++pmu_calls;
    return 25.0f;
}
uint16_t AXP2101_GetDC3Voltage(void) {
    ++pmu_calls;
    return 3000;
}

static void reset_state() {
    voltage_ok = percent_ok = charging_ok = true;
    voltage_mv = 3900;
    percent_value = 78;
    charging_value = false;
    chip_id = 0x4A;
    fuel_gauge_ok = true;
    pmu_calls = 0;
    I2cFault::paused.store(false);
    AuxStop2::restoreFault.store(false);
}
"""

HARNESS_MAIN = """\
static const NanoUI::SensorDataBattery& battery_data(Battery_Driver& driver) {
    return *static_cast<const NanoUI::SensorDataBattery*>(driver.getData());
}

static const NanoUI::SensorDataAXP2101& power_data(AXP2101_Driver& driver) {
    return *static_cast<const NanoUI::SensorDataAXP2101*>(driver.getData());
}

int main() {
    // Fresh state is unknown, not a valid 0%.
    reset_state();
    Battery_Driver initial;
    initial.init();
    assert(!battery_data(initial).is_valid);
    assert(battery_data(initial).percent == -1);
    assert(!battery_data(initial).voltage_valid);
    assert(!battery_data(initial).charging_valid);

    // One healthy sample fills every field independently.
    reset_state();
    Battery_Driver healthy;
    healthy.init();
    healthy.update();
    assert(battery_data(healthy).is_valid);
    assert(battery_data(healthy).voltage_valid);
    assert(battery_data(healthy).charging_valid);
    assert(battery_data(healthy).percent == 78);
    assert(!battery_data(healthy).is_charging);
    assert(battery_data(healthy).voltage > 3.89f && battery_data(healthy).voltage < 3.91f);

    // Gauge failure alone: voltage and charging stay usable, percent is unknown.
    reset_state();
    percent_ok = false;
    Battery_Driver no_gauge;
    no_gauge.init();
    no_gauge.update();
    assert(battery_data(no_gauge).is_valid);
    assert(battery_data(no_gauge).voltage_valid);
    assert(battery_data(no_gauge).charging_valid);
    assert(battery_data(no_gauge).percent == -1);
    assert(!battery_data(no_gauge).is_charging);

    // Voltage failure alone: percent and charging stay usable, voltage is not
    // published as a measured 0 V.
    reset_state();
    voltage_ok = false;
    percent_value = 21;
    charging_value = true;
    Battery_Driver no_voltage;
    no_voltage.init();
    no_voltage.update();
    assert(battery_data(no_voltage).is_valid);
    assert(!battery_data(no_voltage).voltage_valid);
    assert(battery_data(no_voltage).voltage == 0.0f);
    assert(battery_data(no_voltage).percent == 21);
    assert(battery_data(no_voltage).charging_valid);
    assert(battery_data(no_voltage).is_charging);

    // An implausible 0.4 V sample is not accepted as measured voltage.
    reset_state();
    voltage_mv = 400;
    Battery_Driver low_voltage;
    low_voltage.init();
    low_voltage.update();
    assert(!battery_data(low_voltage).voltage_valid);
    assert(battery_data(low_voltage).voltage == 0.0f);
    assert(battery_data(low_voltage).percent == 78);

    // Total read failure publishes no valid field and no 0% level.
    reset_state();
    voltage_ok = percent_ok = charging_ok = false;
    Battery_Driver dead;
    dead.init();
    dead.update();
    assert(!battery_data(dead).is_valid);
    assert(!battery_data(dead).voltage_valid);
    assert(!battery_data(dead).charging_valid);
    assert(battery_data(dead).voltage == 0.0f);
    assert(battery_data(dead).percent == -1);
    assert(!battery_data(dead).is_charging);

    // STOP2 restoration fault fails closed before any PMIC access.
    reset_state();
    AuxStop2::restoreFault.store(true);
    Battery_Driver stopped;
    stopped.init();
    stopped.update();
    assert(!battery_data(stopped).is_valid);
    assert(battery_data(stopped).percent == -1);

    // The AXP composite packet uses the same independent semantics.
    reset_state();
    percent_value = 64;
    charging_value = true;
    AXP2101_Driver pmu;
    pmu.init();
    pmu.update();
    assert(power_data(pmu).is_valid);
    assert(power_data(pmu).voltage_valid);
    assert(power_data(pmu).charging_valid);
    assert(power_data(pmu).percent == 64);
    assert(power_data(pmu).is_charging);

    reset_state();
    percent_ok = false;
    AXP2101_Driver pmu_no_gauge;
    pmu_no_gauge.init();
    pmu_no_gauge.update();
    assert(power_data(pmu_no_gauge).is_valid);
    assert(power_data(pmu_no_gauge).voltage_valid);
    assert(power_data(pmu_no_gauge).charging_valid);
    assert(power_data(pmu_no_gauge).percent == -1);

    // A missing PMIC keeps the packet briefly, then fails closed.
    reset_state();
    AXP2101_Driver pmu_absent;
    pmu_absent.init();
    pmu_absent.update();  // A healthy sample first; init itself no longer sets validity.
    assert(power_data(pmu_absent).is_valid);
    chip_id = 0;
    pmu_absent.update();
    pmu_absent.update();
    pmu_absent.update();
    assert(power_data(pmu_absent).is_valid);
    pmu_absent.update();
    assert(!power_data(pmu_absent).is_valid);
    assert(!power_data(pmu_absent).voltage_valid);
    assert(!power_data(pmu_absent).charging_valid);
    assert(power_data(pmu_absent).percent == -1);

    // Driver init only resets the packet and streak; PMU setup belongs to the
    // board. It must not touch the PMU even when the gauge setup has failed.
    reset_state();
    fuel_gauge_ok = false;
    AXP2101_Driver pmu_no_setup;
    pmu_no_setup.init();
    assert(pmu_calls == 0);
    assert(!power_data(pmu_no_setup).is_valid);
    assert(power_data(pmu_no_setup).percent == -1);
    pmu_no_setup.update();
    assert(pmu_calls > 0);
    assert(power_data(pmu_no_setup).is_valid);
    assert(power_data(pmu_no_setup).percent == 78);

    std::puts("AXP2101 battery/PMU publication semantics PASS");
    return 0;
}
"""


def extract_once(text: str, start: str, end: str) -> str:
    if text.count(start) != 1:
        raise AssertionError(f"expected exactly one {start!r} in hw_sensors.h")
    begin = text.index(start)
    stop = text.index(end, begin)
    return text[begin:stop]


def statement_at(text: str, index: int) -> str:
    """Return the enclosing C statement or braced block around index."""
    start = (
        max(text.rfind(";", 0, index), text.rfind("{", 0, index), text.rfind("}", 0, index))
        + 1
    )
    cursor = index
    while cursor < len(text):
        if text[cursor] == "{":
            depth = 1
            cursor += 1
            while cursor < len(text) and depth:
                if text[cursor] == "{":
                    depth += 1
                elif text[cursor] == "}":
                    depth -= 1
                cursor += 1
            return text[start:cursor]
        if text[cursor] == ";":
            return text[start : cursor + 1]
        cursor += 1
    return text[start:]


def driver_harness() -> str:
    text = HW_SENSORS.read_text(encoding="utf-8")
    battery = extract_once(text, BATTERY_START, BATTERY_END)
    pmu = extract_once(text, AXP_START, AXP_END)
    return HARNESS_PRELUDE + battery + pmu + HARNESS_MAIN


def cpp_compiler() -> str:
    candidate = Path("C:/w64dev/w64devkit/bin/g++.exe")
    if candidate.is_file():
        return str(candidate)
    configured = shutil.which("g++")
    if configured is None:
        raise unittest.SkipTest("an existing local g++ is required")
    return configured


class CheckedVoltageTests(unittest.TestCase):
    def test_production_c_driver(self):
        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler, "existing host C compiler required")
        with tempfile.TemporaryDirectory(prefix="crabit-axp-checked-") as directory:
            temporary = Path(directory)
            shutil.copyfile(AXP_HEADER, temporary / "axp2101.h")
            self.assertEqual(
                AXP_HEADER.read_bytes(), (temporary / "axp2101.h").read_bytes()
            )
            binary = temporary / "axp-checked.exe"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(temporary),
                    "-I",
                    str(FAKES),
                    str(AXP_SOURCE),
                    str(AXP_C_TEST),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("production tests PASS", result.stdout)

    def test_battery_driver_semantics_on_the_host(self):
        with tempfile.TemporaryDirectory(prefix="crabit-axp-semantics-") as directory:
            temporary = Path(directory)
            shutil.copyfile(AXP_HEADER, temporary / "axp2101.h")
            shutil.copyfile(FAKES / "main.h", temporary / "main.h")
            harness = temporary / "axp2101_semantics.cpp"
            harness.write_text(driver_harness(), encoding="utf-8")
            binary = temporary / "axp2101-semantics.exe"
            built = subprocess.run(
                [
                    cpp_compiler(),
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(temporary),
                    "-I",
                    str(NANOUI_INCLUDE),
                    str(harness),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("publication semantics PASS", result.stdout)

    def test_boot_calls_board_and_gauge_init_once_and_keeps_gauge_optional(self):
        source = (ROOT / "firmware/stm32/product/board/board.cpp").read_text(encoding="utf-8")
        begin = source.index("void Board::init() {")
        body = source[begin : source.index("void Board::startTasks()", begin)]

        # Both init entry points are called once, in the active boot path.
        self.assertEqual(body.count("AXP2101_Init()"), 1)
        self.assertEqual(body.count("AXP2101_InitFuelGauge()"), 1)
        self.assertLess(body.index("AXP2101_Init()"), body.index("AXP2101_InitFuelGauge()"))
        self.assertLess(body.index("AXP2101_InitFuelGauge()"), body.index("sensors.registerSensor(&axp_drv);"))

        # Board PMU setup failure remains boot-fatal.
        self.assertIn("Fault::reset", statement_at(body, body.index("AXP2101_Init()")))
        # Optional gauge setup failure must only log, not reset the board.
        self.assertNotIn(
            "Fault::reset", statement_at(body, body.index("AXP2101_InitFuelGauge()"))
        )

    def test_boot_guard_precedes_product_driver_and_ui_initialization(self):
        source = (ROOT / "firmware/stm32/product/board/board.cpp").read_text(encoding="utf-8")
        begin = source.index("    ConfigManager::load();")
        initialization = source[begin : source.index("    AppManager::init(", begin)]
        guard = initialization.index("AXP2101_SetDC1VoltageChecked(3300)")
        failure = initialization.index("Fault::reset(Fault::Reason::I2c);", guard)
        self.assertLess(failure, initialization.index("sensors.registerSensor(&imu_drv);"))
        self.assertNotIn("vTaskSuspend(NULL)", initialization)


if __name__ == "__main__":
    unittest.main()
