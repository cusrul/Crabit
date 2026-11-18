"""Native host test of the actual Galaxion port; no embedded claim."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).with_name("fixtures")
RUNTIME = FIXTURES / "arduboy_port_runtime"


class GalaxionPortTests(unittest.TestCase):
    def test_actual_gameplay_with_real_adapter(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "native g++ required")
        with tempfile.TemporaryDirectory(prefix="crabit-galaxion-") as directory:
            root = Path(directory)
            (root / "app/compat/arduboy").mkdir(parents=True)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h", "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
                shutil.copy2(source / "compat/arduboy" / name,
                             root / "app/compat/arduboy" / name)
            shutil.copytree(source / "apps/games/galaxion",
                            root / "app/apps/games/galaxion")
            for name, destination in (("NanoUI.h", "src/NanoUI.h"),
                                      ("Arduino.h", "Arduino.h"),
                                      ("AmebaStore.h", "AmebaStore.h")):
                shutil.copy2(RUNTIME / name, root / destination)
            exe = root / "galaxion.exe"
            built = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-Werror=int-to-pointer-cast",
                 "-Wno-switch", "-Wno-implicit-fallthrough",
                 "-I", str(root),
                 str(FIXTURES / "arduboy30_galaxion.cpp"), "-o", str(exe)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            images = os.environ.get("CRABIT_ARDUBOY30_GALAXION_FRAMES")
            if images:
                Path(images).mkdir(parents=True, exist_ok=True)
            ran = subprocess.run([str(exe)] + ([images] if images else []),
                                 capture_output=True, text=True, timeout=60)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("galaxion: title/practice", ran.stdout)


if __name__ == "__main__":
    unittest.main()
