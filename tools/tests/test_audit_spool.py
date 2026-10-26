"""Exercise the real bounded RAM spool, without HAL/SDK or a physical device."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AuditSpoolTests(unittest.TestCase):
    def test_real_bw16_relay_with_shared_stm32_store(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="crabit-audit-relay-") as directory:
            binary = Path(directory) / "relay.exe"
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "tools/tests/audit_fakes"),
                    str(ROOT / "tools/tests/audit_relay.cpp"),
                    str(ROOT / "firmware/rtl8720dn/NanoUI/AuditRelay.cpp"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_arduino_ide_contract_matches_shared_authority(self):
        shared = (ROOT / "firmware/shared/audit_contract.h").read_bytes()
        sketch = (ROOT / "firmware/rtl8720dn/NanoUI/audit_contract.h").read_bytes()
        self.assertEqual(shared, sketch)

    def test_real_shared_store(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "existing host C++ compiler required")
        with tempfile.TemporaryDirectory(prefix="crabit-audit-") as directory:
            binary = Path(directory) / "audit.exe"
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(ROOT / "tools/tests/audit_spool.cpp"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_relay_does_not_initialize_radio_or_touch_flash(self):
        for path in ("AuditRelay.cpp", "app/app_ble_sync.h"):
            source = (ROOT / "firmware/rtl8720dn/NanoUI" / path).read_text(encoding="utf-8")
            for forbidden in ("BLE.init(", "BLE.end(", "wifi_off(", "Preferences", "Flash_Write"):
                self.assertNotIn(forbidden, source)
        source = (ROOT / "firmware/rtl8720dn/NanoUI/app/app_wifi_sniff.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("AuditRelay::tick();", source)
        self.assertNotIn("safe_ble_notify", source)


if __name__ == "__main__":
    unittest.main()
