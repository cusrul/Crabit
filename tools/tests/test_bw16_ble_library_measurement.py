"""Ensure the external-library size probe cannot become a runtime entry point."""

import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / "firmware/rtl8720dn/NanoUI/src/ble_spam_size_probe.cpp"


class BleLibraryMeasurementTests(unittest.TestCase):
    def test_probe_is_empty_without_explicit_measurement_flag(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        result = subprocess.run([compiler, "-E", "-P", str(PROBE)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "")

    def test_no_runtime_caller_or_static_spammer_instance(self):
        firmware = ROOT / "firmware/rtl8720dn/NanoUI"
        for path in firmware.rglob("*"):
            if (
                path.suffix not in (".cpp", ".h", ".ino")
                or path == PROBE
                or path.name == "ble_auth.local.h"
            ):
                continue
            self.assertNotIn(
                "crabit_ble_spam_size_probe", path.read_text(encoding="utf-8-sig"), str(path)
            )
        self.assertIn("BW16BLESpammer& spammer", PROBE.read_text())


if __name__ == "__main__":
    unittest.main()
