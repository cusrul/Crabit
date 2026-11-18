"""Host-only runtime for the actual ARDUBULLETs port. Only the device boundary
(canvas, input telemetry, millis) is faked; gameplay code is unmodified."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name("fixtures") / "arduboy30_ardubullets"
RUNTIME = Path(__file__).with_name("fixtures") / "arduboy_port_runtime"
FROZEN_PORTS = ("ardubullets", "bananonsense", "chiemagari", "chribocchi", "evasion",
                "knightmove", "ardugo", "minesweeper", "game2048", "spacetanks",
                "galaxion", "stairssweep", "toyokumono", "rysk", "reversi", "quarto",
                "asteroiddodge", "memoryrhythm", "pong", "jumpboy", "longcat")


class Arduboy30ArdubulletsTest(unittest.TestCase):
    def test_old_six_then_new_include_order(self):
        """Production order: real geometry/Arduino macros, old six, then all ports.

        The copy deliberately gives every staged header one identical mtime.
        A clean checkout or archive extraction can do the same; identical
        content plus equal mtime previously let ``#pragma once`` swallow the
        Evasion helper type even though the class sat in another namespace.
        """
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-arduboy-combined-") as directory:
            root = Path(directory)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            shutil.copytree(source / "compat", root / "app/compat")
            for game in ("arduboy3d", "castle_boy", "hopper", "hollow", "samegame",
                         "lasers", *FROZEN_PORTS):
                shutil.copytree(source / "apps/games" / game, root / "app/apps/games" / game)
            for name in ("NanoUI.h", "Arduino.h", "AmebaStore.h"):
                shutil.copy2(RUNTIME / name, root / ("src" if name == "NanoUI.h" else "") / name)
            # Same size/mtime as a copied tree with low-resolution timestamps.
            stamp = 1_700_000_000
            for header in (root / "app").rglob("*"):
                if header.suffix in (".h", ".hpp"):
                    os.utime(header, (stamp, stamp))
            exe = root / "combined.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-I", str(root),
                 "-I", str(ROOT / "libraries/NanoUI/src"),
                 str(FIXTURE / "old_ports_then_new.cpp"), "-o", str(exe)],
                capture_output=True, text=True, timeout=240)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            for game in FROZEN_PORTS:
                with self.subTest(game=game):
                    run = subprocess.run([str(exe), game], capture_output=True,
                                         text=True, timeout=30)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertIn(f"{game} combined runtime passed", run.stdout)

    def test_pragma_once_helper_headers_are_not_byte_identical(self):
        games = ROOT / "firmware/rtl8720dn/NanoUI/app/apps/games"
        seen = {}
        for game in FROZEN_PORTS:
            for header in sorted((games / game).glob("MyArduboy*.h")):
                data = header.read_bytes()
                # Include-only .inc.h bodies have no pragma once and are parsed
                # once per namespace; only guarded headers can be swallowed.
                if b"#pragma once" not in data:
                    continue
                self.assertNotIn(data, seen, f"{header} duplicates {seen.get(data)}")
                seen[data] = header

    def test_actual_gameplay_runtime(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-ardubullets-") as directory:
            root = Path(directory)
            (root / "app").mkdir()
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            shutil.copytree(source / "compat", root / "app/compat")
            shutil.copytree(source / "apps/games/ardubullets",
                            root / "app/apps/games/ardubullets")
            for name in ("NanoUI.h", "Arduino.h", "AmebaStore.h"):
                shutil.copy2(RUNTIME / name, root / ("src" if name == "NanoUI.h" else "") / name)
            frames = root / "frames"
            frames.mkdir()
            exe = root / "ardubullets.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-I", str(root),
                 "-I", str(ROOT / "libraries/NanoUI/src"), str(FIXTURE / "runtime.cpp"),
                 "-o", str(exe)], capture_output=True, text=True, timeout=90)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe), str(frames)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("ardubullets runtime passed", run.stdout)
            shots = sorted(frames.glob("*.pbm"))
            self.assertEqual(len(shots), 4)
            for shot in shots:
                text = shot.read_text()
                self.assertTrue(text.startswith("P1\n128 64\n"))
                self.assertIn("1", text.split("\n", 2)[2])


if __name__ == "__main__":
    unittest.main()
