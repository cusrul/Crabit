"""Native smoke of all real ports together; no embedded execution claim."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).with_name("fixtures")


class ArduboyPortsSmokeTests(unittest.TestCase):
    def test_three_real_games_with_actual_adapters(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "native g++ required")
        with tempfile.TemporaryDirectory(prefix="crabit-arduboy-games-") as directory:
            root = Path(directory)
            (root / "app/compat/arduboy/legacy").mkdir(parents=True)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h", "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
                shutil.copy2(source / "compat/arduboy" / name, root / "app/compat/arduboy" / name)
            for name in ("ArduboyCore.h", "ArduboyApp.h"):
                shutil.copy2(source / "compat/arduboy/legacy" / name,
                             root / "app/compat/arduboy/legacy" / name)
            for game in ("hollow", "samegame", "lasers"):
                shutil.copytree(source / "apps/games" / game, root / "app/apps/games" / game)
            stub = FIXTURES / "arduboy_port_runtime"
            for name, destination in (("NanoUI.h", "src/NanoUI.h"), ("Arduino.h", "Arduino.h"),
                                      ("AmebaStore.h", "AmebaStore.h")):
                shutil.copy2(stub / name, root / destination)
            exe = root / "games.exe"
            built = subprocess.run([compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                                    "-Wno-unused-parameter", "-I", str(root),
                                    str(FIXTURES / "arduboy_ports_smoke.cpp"), "-o", str(exe)],
                                   capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            images = os.environ.get("CRABIT_ARDUBOY_FRAMES")
            if images:
                Path(images).mkdir(parents=True, exist_ok=True)
            ran = subprocess.run([str(exe)] + ([images] if images else []), capture_output=True,
                                 text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("three actual Arduboy games", ran.stdout)


if __name__ == "__main__":
    unittest.main()
