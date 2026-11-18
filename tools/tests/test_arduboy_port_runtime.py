"""Compile the actual new adapters and unchanged legacy bases with host-only HALs.

No Ameba SDK, EEPROM implementation, device, or global configuration is used.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name("fixtures") / "arduboy_port_runtime"


class ArduboyPortRuntimeTest(unittest.TestCase):
    def test_actual_adapter_runtime(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-arduboy-port-") as directory:
            root = Path(directory)
            (root / "app/compat/arduboy/legacy").mkdir(parents=True)
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app/compat/arduboy"
            for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h", "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
                shutil.copy2(source / name, root / "app/compat/arduboy" / name)
            for name in ("ArduboyCore.h", "ArduboyApp.h"):
                shutil.copy2(source / "legacy" / name, root / "app/compat/arduboy/legacy" / name)
            shutil.copy2(FIXTURE / "NanoUI.h", root / "src/NanoUI.h")
            shutil.copy2(FIXTURE / "Arduino.h", root / "Arduino.h")
            shutil.copy2(FIXTURE / "AmebaStore.h", root / "AmebaStore.h")
            output = root / "port-runtime.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
                 "-I", str(root), str(FIXTURE / "runtime.cpp"), "-o", str(output)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("adapter runtime passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
