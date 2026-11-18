"""Host-only runtime for the actual Memory Rhythm port; no embedded claim."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "firmware/rtl8720dn/NanoUI/app/apps/games/memoryrhythm"
FIXTURE = Path(__file__).with_name("fixtures") / "arduboy30_memoryrhythm"


def builder():
    """Load the build tool's C++ registry reader (single source of metadata)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "bw16_split", ROOT / "tools/build_bw16_split_games.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MemoryRhythmPortTests(unittest.TestCase):
    def test_registry_metadata_and_license(self):
        """The C++ registry, not a per-package app.json, owns id/label/class."""
        _, entries = builder().load_arduboy30_registry(ROOT)
        registry_label, registry_class = entries["memoryrhythm"]
        self.assertEqual(registry_label, "Memory Rhythm")
        self.assertEqual(registry_class, "MemoryRhythmApp")
        header = (GAME / "app.h").read_text(encoding="utf-8")
        self.assertIn(f"class {registry_class} : public ArduboyCompatApp", header)
        self.assertFalse((GAME / "app.json").exists())
        self.assertIn("MIT", (GAME / "LICENSE").read_text(encoding="utf-8"))
        self.assertIn("3cb80b5ce0ec6167522e8f3271eccff351a727b1",
                      (GAME / "README.md").read_text(encoding="utf-8"))

    def test_actual_gameplay_and_adapter(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-memoryrhythm-") as directory:
            root = Path(directory)
            (root / "app/compat/arduboy").mkdir(parents=True)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h", "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
                shutil.copy2(source / "compat/arduboy" / name,
                             root / "app/compat/arduboy" / name)
            shutil.copytree(GAME, root / "app/apps/games/memoryrhythm")
            for name, destination in (("NanoUI.h", "src/NanoUI.h"),
                                      ("Arduino.h", "Arduino.h"),
                                      ("AmebaStore.h", "AmebaStore.h")):
                shutil.copy2(FIXTURE / name, root / destination)
            frames = os.environ.get("CRABIT_ARDUBOY_FRAMES")
            if frames:
                Path(frames).mkdir(parents=True, exist_ok=True)
            executable = root / "memoryrhythm-host.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-Wno-missing-field-initializers",
                 "-I", str(root), "-I", str(ROOT / "libraries/NanoUI/src"),
                 str(FIXTURE / "harness.cpp"), "-o", str(executable)],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)] + ([frames] if frames else []),
                                 capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("memoryrhythm actual game runtime passed", run.stdout)
            if frames:
                shots = sorted(Path(frames).glob("memoryrhythm-*.pbm"))
                self.assertGreaterEqual(len(shots), 9)
                for shot in shots:
                    text = shot.read_text()
                    self.assertTrue(text.startswith("P1\n128 64\n"))
                    self.assertIn("1", text.split("\n", 2)[2])


if __name__ == "__main__":
    unittest.main()
