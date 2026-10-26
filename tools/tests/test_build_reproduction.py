"""Offline checks for clone-time configuration and license packaging."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildReproductionTests(unittest.TestCase):
    def test_original_license_is_packaged(self):
        root_license = (ROOT / "LICENSE").read_text(encoding="utf-8-sig")
        library_license = (ROOT / "libraries/NanoUI/LICENSE").read_text(encoding="utf-8-sig")
        self.assertEqual(root_license, library_license)
        self.assertIn("GNU GENERAL PUBLIC LICENSE", root_license)
        script = (ROOT / "tools/package-nanoui-arduino.ps1").read_text(encoding="utf-8-sig")
        self.assertIn('foreach ($entry in @("LICENSE",', script)

    def test_bundled_dependency_is_selected(self):
        script = (ROOT / "tools/build-rtl8720dn.ps1").read_text(encoding="utf-8-sig")
        self.assertIn('Join-Path $repoRoot "libraries\\BW16_BLE_Spam"', script)
        self.assertIn('$BleSpamLibrary = $bundledBleSpam', script)
        source = ROOT / "libraries/BW16_BLE_Spam/src"
        self.assertTrue((source / "BW16BLESpam.h").is_file())
        self.assertTrue((source / "BW16BLESpam.cpp").is_file())
        self.assertIn("MIT License", (source.parent / "LICENSE").read_text(encoding="utf-8"))

    def test_missing_private_header_fails_before_sdk(self):
        shell = shutil.which("powershell.exe")
        if not shell:
            self.skipTest("Windows PowerShell is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            tools = Path(directory) / "tools"
            tools.mkdir()
            wrapper = tools / "build-rtl8720dn.ps1"
            shutil.copyfile(ROOT / "tools/build-rtl8720dn.ps1", wrapper)
            completed = subprocess.run(
                [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)],
                text=True, capture_output=True, timeout=30,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("Missing private BLE configuration", completed.stderr)
            self.assertNotIn("Arduino CLI compile failed", completed.stderr)


if __name__ == "__main__":
    unittest.main()
