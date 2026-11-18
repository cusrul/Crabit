"""Host tests for the shared Arduboy mono audio adapter.

The production headers are compiled unchanged; only the STM32 wire sink is
replaced by a capture function. No Protocol, SDK, device or embedded build is
involved.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).with_name("fixtures")
RUNTIME = FIXTURES / "arduboy_port_runtime"
APP = ROOT / "firmware/rtl8720dn/NanoUI/app"
SHARED = ROOT / "firmware/shared"

THIRTY_PORT_GAMES = (
    "arduboy3d", "castle_boy", "hopper", "hollow", "samegame", "lasers",
    "ardubullets", "bananonsense", "chiemagari", "chribocchi", "evasion",
    "knightmove", "ardugo", "galaxion", "spacetanks", "minesweeper",
    "game2048", "stairssweep", "toyokumono", "rysk", "reversi", "quarto",
    "asteroiddodge", "memoryrhythm", "pong", "jumpboy", "longcat",
)


def stage_compat(root: Path) -> None:
    (root / "app/compat/arduboy").mkdir(parents=True)
    (root / "src").mkdir()
    for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h",
                 "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
        shutil.copy2(APP / "compat/arduboy" / name,
                     root / "app/compat/arduboy" / name)
    shutil.copy2(RUNTIME / "NanoUI.h", root / "src/NanoUI.h")
    shutil.copy2(RUNTIME / "Arduino.h", root / "Arduino.h")
    shutil.copy2(RUNTIME / "AmebaStore.h", root / "AmebaStore.h")


class ArduboyAudioCompatTest(unittest.TestCase):
    def test_adapter_contract_and_lifecycle(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-arduboy-audio-") as directory:
            root = Path(directory)
            stage_compat(root)
            executable = root / "audio-compat.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-Wno-unused-variable",
                 "-I", str(root), str(FIXTURES / "arduboy_audio_compat.cpp"),
                 "-o", str(executable)],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True,
                                 text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("arduboy audio compat passed", run.stdout)

    def test_sender_constants_match_staged_receiver_contract(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-arduboy-audio-contract-") as directory:
            root = Path(directory)
            (root / "shared").mkdir()
            stage_compat(root)
            for name in ("game_tone.h", "topics.h"):
                shutil.copy2(SHARED / name, root / "shared" / name)
            executable = root / "audio-contract.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-I", str(root),
                 str(FIXTURES / "arduboy_audio_contract.cpp"),
                 "-o", str(executable)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True,
                                 text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_every_real_port_owns_and_releases_audio(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-arduboy-audio-games-") as directory:
            root = Path(directory)
            (root / "app/apps/games").mkdir(parents=True)
            stage_compat(root)
            for game in THIRTY_PORT_GAMES:
                shutil.copytree(APP / "apps/games" / game,
                                root / "app/apps/games" / game)
            executable = root / "audio-games.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-Wno-unused-variable",
                 "-Wno-unused-function", "-Wno-missing-field-initializers",
                 "-Wno-reorder", "-I", str(root),
                 str(FIXTURES / "arduboy_audio_games.cpp"),
                 "-o", str(executable)],
                capture_output=True, text=True, timeout=180,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True,
                                 text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("arduboy audio games passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
