"""Compile the actual Chri-Bocchi Cat port with the real compat layer on host.

Only Arduino timing/random, NanoUI's Canvas/InputDriver boundary and AmebaStore
telemetry are faked. No SDK, EEPROM implementation, device or embedded build is
used here; hardware acceptance remains a separate check.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "firmware/rtl8720dn/NanoUI/app/apps/games/chribocchi"
FIXTURE = Path(__file__).with_name("fixtures") / "arduboy30_chribocchi"


def builder():
    """Load the build tool's C++ registry reader (single source of metadata)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "bw16_split", ROOT / "tools/build_bw16_split_games.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ChribocchiPortTests(unittest.TestCase):
    def test_registry_metadata_and_license(self):
        """The C++ registry, not a per-package app.json, owns id/label/class."""
        _, entries = builder().load_arduboy30_registry(ROOT)
        registry_label, registry_class = entries["chribocchi"]
        self.assertEqual(registry_label, "Chri-Bocchi Cat")
        self.assertEqual(registry_class, "ChribocchiApp")
        header = (GAME / "app.h").read_text(encoding="utf-8")
        self.assertIn(f"class {registry_class} : public ArduboyCompatApp", header)
        self.assertFalse((GAME / "app.json").exists())
        self.assertIn("MIT", (GAME / "LICENSE").read_text(encoding="utf-8"))
        self.assertIn("d4b1f041789dcd1d71907654e4025d613b4ab420",
                      (GAME / "README.md").read_text(encoding="utf-8"))

    def test_actual_gameplay_and_adapter(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-chribocchi-") as directory:
            root = Path(directory)
            (root / "app/compat/arduboy").mkdir(parents=True)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h", "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
                shutil.copy2(source / "compat/arduboy" / name,
                             root / "app/compat/arduboy" / name)
            shutil.copytree(GAME, root / "app/apps/games/chribocchi")
            for name, destination in (("NanoUI.h", "src/NanoUI.h"),
                                      ("Arduino.h", "Arduino.h"),
                                      ("AmebaStore.h", "AmebaStore.h")):
                shutil.copy2(FIXTURE / name, root / destination)
            executable = root / "chribocchi-host.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-I", str(root),
                 str(FIXTURE / "harness.cpp"), "-o", str(executable)],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            frames = os.environ.get("CRABIT_ARDUBOY_FRAMES")
            if frames:
                Path(frames).mkdir(parents=True, exist_ok=True)
            run = subprocess.run([str(executable)] + ([frames] if frames else []),
                                 capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("chribocchi actual game", run.stdout)


if __name__ == "__main__":
    unittest.main()
