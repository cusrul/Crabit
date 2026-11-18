"""Host-only runtime for the actual Evasion port; no embedded execution claim."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name("fixtures") / "arduboy30_evasion"
RUNTIME = Path(__file__).with_name("fixtures") / "arduboy_port_runtime"


class Arduboy30EvasionTest(unittest.TestCase):
    def test_actual_gameplay_runtime(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-evasion-") as directory:
            root = Path(directory)
            (root / "app").mkdir()
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            shutil.copytree(source / "compat", root / "app/compat")
            shutil.copytree(source / "apps/games/evasion", root / "app/apps/games/evasion")
            for name in ("NanoUI.h", "Arduino.h", "AmebaStore.h"):
                shutil.copy2(RUNTIME / name, root / ("src" if name == "NanoUI.h" else "") / name)
            frames = root / "frames"
            frames.mkdir()
            exe = root / "evasion.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-I", str(root),
                 "-I", str(ROOT / "libraries/NanoUI/src"), str(FIXTURE / "runtime.cpp"),
                 "-o", str(exe)], capture_output=True, text=True, timeout=90)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe), str(frames)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("evasion runtime passed", run.stdout)
            shots = sorted(frames.glob("*.pbm"))
            self.assertEqual(len(shots), 2)
            for shot in shots:
                text = shot.read_text()
                self.assertTrue(text.startswith("P1\n128 64\n"))
                self.assertIn("1", text.split("\n", 2)[2])


if __name__ == "__main__":
    unittest.main()
