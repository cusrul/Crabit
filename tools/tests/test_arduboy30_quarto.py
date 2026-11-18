"""Compile and run the real Quarto! port with the real compat adapter.

Only Arduino/NanoUI/AmebaStore hardware boundaries are stubbed. The real
NanoUI geometry header and the launcher's inherited macros are included first to
catch include-order/type/macro regressions. No embedded build or device.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).with_name("fixtures")
STUB = FIXTURES / "arduboy_port_runtime"
GAME = "quarto"


class Arduboy30QuartoTest(unittest.TestCase):
    def test_actual_game_flow(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-quarto-") as directory:
            root = Path(directory)
            (root / "app/compat/arduboy/legacy").mkdir(parents=True)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            (root / "NanoUI/compatibility").mkdir(parents=True)
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h", "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
                shutil.copy2(source / "compat/arduboy" / name, root / "app/compat/arduboy" / name)
            for name in ("ArduboyCore.h", "ArduboyApp.h"):
                shutil.copy2(source / "compat/arduboy/legacy" / name,
                             root / "app/compat/arduboy/legacy" / name)
            shutil.copytree(source / "apps/games" / GAME, root / "app/apps/games" / GAME)
            shutil.copy2(STUB / "NanoUI.h", root / "src/NanoUI.h")
            shutil.copy2(STUB / "Arduino.h", root / "Arduino.h")
            shutil.copy2(STUB / "AmebaStore.h", root / "AmebaStore.h")
            shutil.copy2(ROOT / "libraries/NanoUI/src/NanoUI/compatibility/geometry.h",
                         root / "NanoUI/compatibility/geometry.h")
            exe = root / "quarto.exe"
            built = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-Wno-parentheses", "-I", str(root),
                 str(FIXTURES / "arduboy30_quarto.cpp"), "-o", str(exe)],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            frames = os.environ.get("CRABIT_ARDUBOY_FRAMES")
            args = [str(exe)] + ([frames] if frames else [])
            if frames:
                Path(frames).mkdir(parents=True, exist_ok=True)
            ran = subprocess.run(args, capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("quarto:", ran.stdout)
            self.assertIn("reentry passed", ran.stdout)


if __name__ == "__main__":
    unittest.main()
