"""Compile and run the real B-primary ports with the real compat adapter.

Each fixture includes the actual game `app.h` and drives the shared adapter with
the STM32-resolved PAT bits: bit 4 is one single tap, bit 5 is one double tap.
The tests assert the single tap performs that game's real B action (check,
place, remove, summon, rotate, fast-roll, restart, polarity toggle, fire, pick)
and that the double tap is the real A action (menu, cancel, or strafe), so a
policy flip cannot be masked by string checks.

Only Arduino/NanoUI/AmebaStore hardware boundaries are stubbed. No embedded
build or device.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).with_name("fixtures")
PRIMARY = FIXTURES / "arduboy_primary_actions"
STUB = FIXTURES / "arduboy_port_runtime"

GAMES = (
    "bananonsense",
    "evasion",
    "knightmove",
    "quarto",
    "reversi",
    "samegame",
    "toyokumono",
    "stairssweep",
    "lasers",
    "arduboy3d",
    "chiemagari",
)


class ArduboyPrimaryActionsTest(unittest.TestCase):
    def test_single_tap_runs_each_game_b_action(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-primary-actions-") as directory:
            root = Path(directory)
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            (root / "app/compat/arduboy/legacy").mkdir(parents=True)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            (root / "NanoUI/compatibility").mkdir(parents=True)
            for name in sorted(p.name for p in (source / "compat/arduboy").glob("*.h")):
                shutil.copy2(source / "compat/arduboy" / name,
                             root / "app/compat/arduboy" / name)
            for name in ("ArduboyCore.h", "ArduboyApp.h"):
                shutil.copy2(source / "compat/arduboy/legacy" / name,
                             root / "app/compat/arduboy/legacy" / name)
            for game in GAMES:
                shutil.copytree(source / "apps/games" / game,
                                root / "app/apps/games" / game)
            shutil.copy2(STUB / "NanoUI.h", root / "src/NanoUI.h")
            shutil.copy2(STUB / "Arduino.h", root / "Arduino.h")
            shutil.copy2(STUB / "AmebaStore.h", root / "AmebaStore.h")
            shutil.copy2(ROOT / "libraries/NanoUI/src/NanoUI/compatibility/geometry.h",
                         root / "NanoUI/compatibility/geometry.h")

            for game in GAMES:
                with self.subTest(game=game):
                    exe = root / (game + ".exe")
                    built = subprocess.run(
                        [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                         "-Wno-unused-parameter", "-Wno-parentheses", "-I", str(root),
                         str(PRIMARY / (game + ".cpp")), "-o", str(exe)],
                        capture_output=True, text=True, timeout=180,
                    )
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    ran = subprocess.run([str(exe)], capture_output=True, text=True,
                                         timeout=60)
                    self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                    self.assertIn(game + " primary action passed", ran.stdout)


if __name__ == "__main__":
    unittest.main()
