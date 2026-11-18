"""Host tests for the full upstream Hopper port; no embedded or hardware claim.

The runtime fixture exercises the real logo/title/record/50-floor game code
against the actual ArduboyCompatApp lifecycle and the current shared
ArduboyPortRuntime.h, which must expose Hopper's id 21 without test patching.
"""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).with_name("fixtures")
RUNTIME = FIXTURES / "arduboy_port_runtime"
GAME = ROOT / "firmware/rtl8720dn/NanoUI/app/apps/games/hopper"
APP = ROOT / "firmware/rtl8720dn/NanoUI/app"
LICENSE_SHA256 = "62a5db2ff173434f94d055a9432f5655267e92f4db92a042b832884e8a48f6bb"


def stage(root: Path) -> None:
    (root / "app/compat").mkdir(parents=True)
    (root / "app/apps/games").mkdir(parents=True)
    (root / "src").mkdir()
    shutil.copytree(APP / "compat", root / "app/compat", dirs_exist_ok=True)
    shutil.copytree(GAME, root / "app/apps/games/hopper")
    for name, destination in (("NanoUI.h", "src/NanoUI.h"),
                              ("Arduino.h", "Arduino.h"),
                              ("AmebaStore.h", "AmebaStore.h")):
        shutil.copy2(RUNTIME / name, root / destination)


def live_runtime_port_count() -> int:
    """Read the shared runtime's real slot count; staging never rewrites it."""
    header = APP / "compat/arduboy/ArduboyPortRuntime.h"
    match = re.search(r"constexpr uint8_t kPortIdCount = (\d+);",
                      header.read_text(encoding="utf-8"))
    assert match, "runtime port-id count not found"
    return int(match.group(1))


class HopperFullSourceTests(unittest.TestCase):
    def test_package_is_full_upstream_not_simplified_remake(self):
        self.assertEqual(hashlib.sha256((GAME / "LICENSE").read_bytes()).hexdigest(),
                         LICENSE_SHA256)
        self.assertFalse((GAME / "app.json").exists())
        app = (GAME / "app.h").read_text(encoding="utf-8")
        common = (GAME / "common.h").read_text(encoding="utf-8")
        game = (GAME / "game.inc.h").read_text(encoding="utf-8")
        title = (GAME / "title.inc.h").read_text(encoding="utf-8")
        logo = (GAME / "logo.inc.h").read_text(encoding="utf-8")
        port = (GAME / "port.h").read_text(encoding="utf-8")
        readme = (GAME / "README.md").read_text(encoding="utf-8")

        self.assertIn("class AppHopper : public ArduboyCompatApp", app)
        self.assertIn("static constexpr uint8_t kPortId = 21;", app)
        # Id 21 is the next free catalogue slot; the live shared runtime must
        # expose it (22 slots total). Tests compile that header unchanged.
        self.assertGreaterEqual(live_runtime_port_count(), 22)
        used = {}
        for other in (APP / "apps/games").glob("*/app.h"):
            if other.parent == GAME:
                continue
            match = re.search(r"kPortId = (\d+);", other.read_text(encoding="utf-8"))
            if match:
                used[int(match.group(1))] = other.parent.name
        self.assertNotIn(21, used, used)
        self.assertIn("createPortState<PortState>(kPortId)", app)
        self.assertIn("attachSaveSlot", app)
        self.assertIn("adoptPortState", app)
        self.assertIn("uint8_t setLastScore", common)
        self.assertIn("#define FLOORS_LEVEL    50", common)
        for marker in ("FLRTYPE_NORMAL", "FLRTYPE_MOVE", "FLRTYPE_VANISH",
                       "FLRTYPE_SMALL", "FLRTYPE_GOAL"):
            self.assertIn(marker, game)
        for marker in ("STATE_CLEAR", "STATE_OVER", "STATE_PAUSE",
                       "initLevel(false)", "springPlayer", "soundClear"):
            self.assertIn(marker, game)
        self.assertIn("EEPROM_ADDR_BASE    800", title)
        self.assertIn("BEST 10 SCORES", title)
        self.assertIn("CREDIT", title)
        self.assertIn("imgOBN", logo)
        self.assertIn("SIGNAL_PTN", logo)
        self.assertIn("struct HopperGameState", port)
        self.assertIn("FLOOR floorAry[FLOORS_MANAGE]", port)
        self.assertIn("CHIP chipAry[CHIPS]", port)
        # The mutable arrays are in the typed State, not namespace statics.
        self.assertNotIn("static FLOOR    floorAry", game)
        self.assertNotIn("static CHIP     chipAry", game)
        self.assertNotIn("native NanoUI remake", readme)
        self.assertIn("full", readme.lower())


class HopperFullRuntimeTests(unittest.TestCase):
    def test_real_gameplay_lifecycle_save_and_reentry(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-hopper-full-") as directory:
            root = Path(directory)
            stage(root)
            # The staged production header is the real shared header; it is
            # never patched to invent a Hopper slot.
            self.assertGreaterEqual(live_runtime_port_count(), 22)
            exe = root / "hopper_full.exe"
            built = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-Wno-unused-variable",
                 "-Wno-unused-function", "-Werror=int-to-pointer-cast",
                 "-I", str(root), str(FIXTURES / "hopper_full_runtime.cpp"),
                 "-o", str(exe)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            images = os.environ.get("CRABIT_HOPPER_FULL_FRAMES")
            if images:
                Path(images).mkdir(parents=True, exist_ok=True)
            ran = subprocess.run([str(exe)] + ([images] if images else []),
                                 capture_output=True, text=True, timeout=90)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("hopper full runtime passed", ran.stdout)
            if images:
                shots = sorted(Path(images).glob("hopper-full-*.pbm"))
                self.assertGreaterEqual(len(shots), 5)
                for shot in shots:
                    data = shot.read_bytes()
                    self.assertTrue(data.startswith(b"P4\n128 64\n"))
                    self.assertTrue(any(data[11:]))

    def test_old_six_and_all_catalogue_headers_stage_together(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-hopper-order-") as directory:
            root = Path(directory)
            (root / "app/compat").mkdir(parents=True)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            shutil.copytree(APP / "compat", root / "app/compat", dirs_exist_ok=True)
            for game in ("arduboy3d", "castle_boy", "hopper", "hollow", "samegame",
                         "lasers", "ardubullets", "bananonsense", "chiemagari",
                         "chribocchi", "evasion", "knightmove", "ardugo",
                         "minesweeper", "game2048", "spacetanks", "galaxion",
                         "stairssweep", "toyokumono", "rysk", "reversi",
                         "quarto", "asteroiddodge", "memoryrhythm", "pong",
                         "jumpboy", "longcat"):
                shutil.copytree(APP / "apps/games" / game,
                                root / "app/apps/games" / game)
            for name, destination in (("NanoUI.h", "src/NanoUI.h"),
                                      ("Arduino.h", "Arduino.h"),
                                      ("AmebaStore.h", "AmebaStore.h")):
                shutil.copy2(RUNTIME / name, root / destination)
            # A archive/checkout can flatten timestamps; equal mtime and
            # identical helper names must not change include-order behaviour.
            stamp = 1_700_000_000
            for header in (root / "app").rglob("*"):
                if header.suffix in (".h", ".hpp"):
                    os.utime(header, (stamp, stamp))
            exe = root / "hopper_order.exe"
            built = subprocess.run(
                [compiler, "-std=c++11", "-O0", "-w", "-I", str(root),
                 str(FIXTURES / "hopper_full_include_order.cpp"), "-o", str(exe)],
                capture_output=True, text=True, timeout=180)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True,
                                 timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
