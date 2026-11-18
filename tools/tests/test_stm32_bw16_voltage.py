"""Guards for the Settings BW16 DCDC3 target-voltage setting.

The UI only stores 3.0/3.1/3.2 V in a local trailer. Normal boot and the flash
bridge both consume that target at their existing PMU write sites,
and the AXP2101 init keeps its fixed preconfiguration. Host behaviour is
exercised by test_stm32_settings_flat.py; this module pins the source boundaries.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_bw16_uart_ingress import extract_function

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "firmware/stm32/Core/NanoUI/app_real"
BOOT = UI / "app_coproc.h"
APP = UI / "app_settings.h"
POWER_STATE = UI / "components/bw16_power_state.h"
LAUNCHER = UI / "launcher.h"
BOARD = ROOT / "firmware/stm32/product/board/board.cpp"
BRIDGE = ROOT / "firmware/stm32/Core/Src/app_bw16_flash.cpp"
AXP = ROOT / "firmware/stm32/Core/Src/axp2101.c"
HEADER = ROOT / "firmware/stm32/Core/Inc/bw16_voltage_config.h"
CONFIG = ROOT / "firmware/stm32/Core/Inc/sys_config.h"

AUTHORIZED_VOLTAGE_CALL = "AXP2101_SetDC3Voltage(Crabit::Bw16Voltage::targetMv());"


class Bw16VoltageBoundaryTests(unittest.TestCase):
    def test_power_domain_uses_the_saved_target_in_the_original_order(self):
        text = BOOT.read_text(encoding="utf-8")
        enable = extract_function(text, "inline void BW16_EnablePowerDomain(")
        self.assertEqual(enable.count(AUTHORIZED_VOLTAGE_CALL), 1)
        self.assertNotIn("3100", enable)
        self.assertLess(
            enable.index("AXP2101_SetDC3Voltage"), enable.index("AXP2101_SetDC3Enable(true)")
        )
        self.assertIn('#include "bw16_voltage_config.h"', text)

        boot = extract_function(text, "inline void BW16_NormalBoot(")
        sequence = (
            "BW16_PrimeBootState();",
            "BW16_SetBusConnect(true);",
            "BW16_SetPower(false);",
            "BW16_EnablePowerDomain();",
            "vTaskDelay(pdMS_TO_TICKS(150));",
            "Uart::configure();",
            "BW16_SetPower(true);",
        )
        for step in sequence:
            self.assertIn(step, boot)
        positions = [boot.index(step) for step in sequence]
        self.assertEqual(positions, sorted(positions))
        for helper in ("inline void BW16_PrimeBootState(", "inline void BW16_ForceOff("):
            body = extract_function(text, helper)
            self.assertIn("BW16_DisablePowerDomain();", body)
            self.assertIn("vTaskDelay(", body)

    def test_flash_bridge_uses_saved_target_without_changing_sequence(self):
        bridge = BRIDGE.read_text(encoding="utf-8")
        self.assertEqual(bridge.count(AUTHORIZED_VOLTAGE_CALL), 1)
        self.assertIn('#include "bw16_voltage_config.h"', bridge)
        self.assertIn("AXP2101_SetDC3Enable(true);", bridge)
        self.assertNotIn("Bw16Power", bridge)

        # Pre-feature bridge boot I/O, captured from the private baseline revision
        # 372f92e. The published history does not contain that revision, so the
        # expected sequence is pinned here instead of being read back with git.
        expected_boot_io = [
            "HAL_Delay(200);",
            "HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);",
            "HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);",
            "HAL_Delay(500);",
            "HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);",
            "HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);",
            "HAL_Delay(500); // 确保 BW16 内部彻底放电",
            "AXP2101_SetDC3Voltage(3000);",
            "AXP2101_SetDC3Enable(true);",
            "HAL_Delay(100);",
            "HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);",
            "HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, GPIO_PIN_RESET);",
            "HAL_Delay(100);",
            "HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, GPIO_PIN_SET);",
            "HAL_Delay(100);",
            "HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, GPIO_PIN_RESET);",
            "HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);",
            "HAL_Delay(1000); // 稳稳等待 BW16 进入 Bootloader 模式",
            "HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);",
        ]

        def boot_io(function_body):
            end = function_body.index("MX_USART1_UART_Init();")
            calls = []
            for line in function_body[:end].splitlines():
                text = line.strip()
                if any(marker in text for marker in (
                    "HAL_GPIO_Init(", "HAL_GPIO_WritePin(", "HAL_Delay(",
                    "AXP2101_SetDC3Voltage(", "AXP2101_SetDC3Enable(",
                )):
                    calls.append(text.replace(AUTHORIZED_VOLTAGE_CALL,
                                              "AXP2101_SetDC3Voltage(3000);"))
            return calls

        new = extract_function(bridge, "bool BW16_Dedicated_Bridge()")
        self.assertEqual(boot_io(new), expected_boot_io)
        board = BOARD.read_text(encoding="utf-8")
        self.assertLess(board.index("ConfigManager::load();"), board.index("AppManager::init("))

    def test_axp_init_keeps_fixed_3000_and_dcdc3_off(self):
        axp = AXP.read_text(encoding="utf-8")
        init = extract_function(axp, "HAL_StatusTypeDef AXP2101_Init(")
        self.assertIn("AXP2101_SetDC3Voltage(3000);", init)
        self.assertIn("AXP2101_WriteReg(0x80, 0x01);", init)
        self.assertNotIn("targetMv", init)
        self.assertNotIn("Bw16Voltage", init)

    def test_legacy_dcdc3_step_app_is_not_registered(self):
        # Existing 500..3400 mV debug header is intentionally left untouched but
        # must stay unreachable from the product launcher/board.
        for source in (LAUNCHER, BOARD):
            text = source.read_text(encoding="utf-8")
            self.assertNotIn("app_dcdc", text)

    def test_ui_reads_pmu_and_never_writes_it(self):
        app = APP.read_text(encoding="utf-8")
        for forbidden in ("AXP2101_", "SetDC3", "app_coproc.h", "FreeRTOS.h", "task.h"):
            self.assertNotIn(forbidden, app)
        state = POWER_STATE.read_text(encoding="utf-8")
        self.assertIn("AXP2101_ReadReg(AXP2101_DC_ONOFF_DVM_CTRL", state)
        self.assertIn("0x04u", state)
        self.assertNotIn("AXP2101_WriteReg", state)
        self.assertNotIn("AXP2101_SetDC3Voltage(", state)
        self.assertNotIn("AXP2101_SetDC3Enable(", state)
        for marker in (
            "g_coproc_active",
            "amebaSessionActive()",
            "CompanionBle::active()",
            "CompanionBle::transitioning()",
            "Availability::Unreadable",
        ):
            self.assertIn(marker, state)

    def test_local_voltage_trailer_contract(self):
        header = HEADER.read_text(encoding="utf-8")
        self.assertIn("static_assert(sizeof(Record) == 8", header)
        self.assertIn("0x31564257u", header)
        self.assertIn("levels[] = {3000, 3100, 3200}", header)
        self.assertIn("record.code < level_count", header)
        self.assertIn("record.checksum == checksum(record)", header)
        self.assertIn("if (value >= level_count)", header)

        config = CONFIG.read_text(encoding="utf-8")
        self.assertIn('#include "bw16_voltage_config.h"', config)
        self.assertIn("Crabit::Bw16Voltage::load(", config)
        self.assertIn("Crabit::Bw16Voltage::sealed()", config)
        self.assertIn("sizeof(Crabit::Bw16Voltage::Record)", config)
        self.assertIn("CONFIG_FLASH_ADDR + sizeof(SystemConfig) + sizeof(CrabitPreferences::Record)",
                      config)
        for layout in (
            "offsetof(SystemConfig, brightness) == 36",
            "offsetof(SystemConfig, enable_ble) == 41",
            "offsetof(SystemConfig, watch_face) == 42",
            "sizeof(SystemConfig) == 48",
        ):
            self.assertIn(layout, config)

    def test_settings_page_stores_only_after_apply(self):
        app = APP.read_text(encoding="utf-8")
        self.assertEqual(app.count("ConfigManager::save()"), 1)
        self.assertIn("Crabit::Bw16Voltage::setIndex(voltage_index)", app)
        self.assertIn("Crabit::Bw16Voltage::current = previous_voltage", app)
        self.assertIn("const Crabit::Bw16Voltage::Record previous_voltage", app)
        self.assertIn("Crabit::Bw16Power::availability()", app)
        self.assertIn("if (voltage_pending && !voltage_blocked)", app)
        self.assertIn("if (voltage_blocked && !otherChanged())", app)


class Bw16VoltageBootConsumptionTests(unittest.TestCase):
    def compiler(self):
        configured = shutil.which("g++")
        candidate = Path("C:/w64dev/w64devkit/bin/g++.exe")
        if candidate.is_file():
            return str(candidate)
        self.assertIsNotNone(configured, "an existing local g++ is required")
        return configured

    def test_boot_helper_consumes_only_the_stored_target(self):
        # Extract the production helper and execute it against PMU spies. This
        # proves the saved 3.0/3.1/3.2 V value (and nothing else) reaches DCDC3.
        helper = extract_function(
            BOOT.read_text(encoding="utf-8"), "inline void BW16_EnablePowerDomain("
        )
        self.assertIn(AUTHORIZED_VOLTAGE_CALL, helper)
        source_text = (
            "#include <cassert>\n"
            "#include <cstdint>\n"
            "#include <cstdio>\n"
            '#include "bw16_voltage_config.h"\n'
            "static uint16_t last_mv = 0; static int enable_calls = 0;\n"
            "inline void AXP2101_SetDC3Voltage(uint16_t mv) { last_mv = mv; }\n"
            "inline void AXP2101_SetDC3Enable(bool enable) { if (enable) ++enable_calls; }\n"
            + helper
            + "\nint main() {\n"
            "    using namespace Crabit::Bw16Voltage;\n"
            "    Record record{}; record.code = 1; record.checksum = checksum(record);\n"
            "    load(record); BW16_EnablePowerDomain();\n"
            "    assert(last_mv == 3100 && enable_calls == 1);\n"
            "    load(Record{}); BW16_EnablePowerDomain();\n"
            "    assert(last_mv == 3000 && enable_calls == 2);\n"
            "    Record bad{}; bad.code = 2; bad.checksum ^= 1; load(bad);\n"
            "    BW16_EnablePowerDomain();\n"
            "    assert(last_mv == 3000 && enable_calls == 3);\n"
            "    Record top{}; top.code = 2; top.checksum = checksum(top); load(top);\n"
            "    BW16_EnablePowerDomain();\n"
            "    assert(last_mv == 3200 && enable_calls == 4);\n"
            '    std::puts("BW16 voltage boot consumption checks passed");\n'
            "    return 0;\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-voltage-") as tmp:
            source = Path(tmp) / "boot.cpp"
            binary = Path(tmp) / "boot.exe"
            source.write_text(source_text, encoding="utf-8")
            built = subprocess.run(
                [
                    self.compiler(),
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-I",
                    str(ROOT / "firmware/stm32/Core/Inc"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("BW16 voltage boot consumption checks passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
