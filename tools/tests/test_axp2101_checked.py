"""Compile the full production PMIC C driver against a register-level I2C fake."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CheckedVoltageTests(unittest.TestCase):
    def test_production_c_driver(self):
        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler, "existing host C compiler required")
        with tempfile.TemporaryDirectory(prefix="crabit-axp-checked-") as directory:
            temporary = Path(directory)
            header = ROOT / "firmware/stm32/Core/Inc/axp2101.h"
            shutil.copyfile(header, temporary / "axp2101.h")
            self.assertEqual(header.read_bytes(), (temporary / "axp2101.h").read_bytes())
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
                    str(ROOT / "tools/tests/power_driver_fakes"),
                    str(ROOT / "firmware/stm32/Core/Src/axp2101.c"),
                    str(ROOT / "tools/tests/axp2101_checked.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("production tests PASS", result.stdout)

    def test_boot_guard_precedes_product_driver_and_ui_initialization(self):
        source = (ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp").read_text(encoding="utf-8")
        begin = source.index("    ConfigManager::load();")
        initialization = source[begin : source.index("    OLED_Init();", begin)]
        guard = initialization.index("AXP2101_SetDC1VoltageChecked(&hi2c1, 3300)")
        failure = initialization.index("vTaskSuspend(NULL);", guard)
        self.assertLess(failure, initialization.index("imu_drv.init();"))
        self.assertIn("return;", initialization[failure : initialization.index("imu_drv.init();")])


if __name__ == "__main__":
    unittest.main()
