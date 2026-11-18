"""Host-only runtime for the actual Asteroid Dodge port; no embedded claim."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "firmware/rtl8720dn/NanoUI/app/apps/games/asteroiddodge"
FIXTURE = Path(__file__).with_name("fixtures") / "arduboy30_asteroiddodge"

# Text the screens really print. The split linker attributes read-only game
# data by namespace, so each of these must exist as its own namespace-scoped
# object rather than an anonymous literal in the merged string section.
NAMESPACE = "AsteroidDodgeSandbox"
READONLY_TEXT = (
    "titleText",
    "playLabel",
    "hiLabel",
    "aboutLabel",
    "muteLabel",
    "byText",
    "githubText",
    "projectText",
    "continueText",
    "gameOverText",
    "scoreLabel",
)


def builder():
    """Load the build tool's C++ registry reader (single source of metadata)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "bw16_split", ROOT / "tools/build_bw16_split_games.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AsteroidDodgePortTests(unittest.TestCase):
    def test_registry_metadata_and_license(self):
        """The C++ registry, not a per-package app.json, owns id/label/class."""
        _, entries = builder().load_arduboy30_registry(ROOT)
        registry_label, registry_class = entries["asteroiddodge"]
        self.assertEqual(registry_label, "Asteroid Dodge")
        self.assertEqual(registry_class, "AsteroidDodgeApp")
        header = (GAME / "app.h").read_text(encoding="utf-8")
        self.assertIn(f"class {registry_class} : public ArduboyCompatApp", header)
        self.assertFalse((GAME / "app.json").exists())
        self.assertIn("MIT", (GAME / "LICENSE").read_text(encoding="utf-8"))
        self.assertIn("830152255d35489b601c1461defa7d1b794ee17a",
                      (GAME / "README.md").read_text(encoding="utf-8"))

    def test_actual_gameplay_and_adapter(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-asteroiddodge-") as directory:
            root = Path(directory)
            (root / "app/compat/arduboy").mkdir(parents=True)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h", "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
                shutil.copy2(source / "compat/arduboy" / name,
                             root / "app/compat/arduboy" / name)
            shutil.copytree(GAME, root / "app/apps/games/asteroiddodge")
            for name, destination in (("NanoUI.h", "src/NanoUI.h"),
                                      ("Arduino.h", "Arduino.h"),
                                      ("AmebaStore.h", "AmebaStore.h")):
                shutil.copy2(FIXTURE / name, root / destination)
            frames = os.environ.get("CRABIT_ARDUBOY_FRAMES")
            if frames:
                Path(frames).mkdir(parents=True, exist_ok=True)
            executable = root / "asteroiddodge-host.exe"
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
            self.assertIn("asteroiddodge actual game runtime passed", run.stdout)
            if frames:
                shots = sorted(Path(frames).glob("asteroiddodge-*.pbm"))
                self.assertGreaterEqual(len(shots), 5)
                for shot in shots:
                    text = shot.read_text()
                    self.assertTrue(text.startswith("P1\n128 64\n"))
                    self.assertIn("1", text.split("\n", 2)[2])

    def test_readonly_text_is_namespace_attributed(self):
        """Displayed text must be namespace-scoped read-only game data.

        The real split build links `.rodata.*<mangled-namespace>*` into the
        extension and then requires both code and readonly assets for every
        game namespace. Anonymous literals merge into the shared string
        section, so this compiles the port's own translation unit (no test
        driver) and checks that each screen string is a named object in its own
        section and that the game code relocates against it.
        """
        compiler = shutil.which("g++")
        objdump = shutil.which("objdump")
        if not compiler or not objdump:
            self.skipTest("g++/objdump are unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-asteroiddodge-text-") as directory:
            root = Path(directory)
            (root / "app/compat/arduboy").mkdir(parents=True)
            (root / "app/apps/games").mkdir(parents=True)
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app"
            for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h", "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
                shutil.copy2(source / "compat/arduboy" / name,
                             root / "app/compat/arduboy" / name)
            shutil.copytree(GAME, root / "app/apps/games/asteroiddodge")
            for name, destination in (("NanoUI.h", "src/NanoUI.h"),
                                      ("Arduino.h", "Arduino.h"),
                                      ("AmebaStore.h", "AmebaStore.h")):
                shutil.copy2(FIXTURE / name, root / destination)
            obj = root / "port.o"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O2", "-fdata-sections",
                 "-ffunction-sections", "-c",
                 "-I", str(root), "-I", str(ROOT / "libraries/NanoUI/src"),
                 str(FIXTURE / "assets.cpp"), "-o", str(obj)],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)

            headers = subprocess.run([objdump, "-h", str(obj)],
                                     capture_output=True, text=True, timeout=60)
            self.assertEqual(headers.returncode, 0, headers.stdout + headers.stderr)
            sections = {}
            for line in headers.stdout.splitlines():
                fields = line.split()
                if len(fields) >= 3 and fields[1].startswith((".rodata", ".rdata")):
                    try:
                        sections[fields[1]] = int(fields[2], 16)
                    except ValueError:
                        continue
            relocations = subprocess.run([objdump, "-r", str(obj)],
                                         capture_output=True, text=True, timeout=60)
            self.assertEqual(relocations.returncode, 0,
                             relocations.stdout + relocations.stderr)

            for name in READONLY_TEXT:
                symbol = f"_ZN{len(NAMESPACE)}{NAMESPACE}L{len(name)}{name}E"
                owned = {section: size for section, size in sections.items()
                         if symbol in section}
                self.assertTrue(owned, f"{name} is not a named readonly object")
                self.assertGreaterEqual(max(owned.values()), 2,
                                        f"{name} has no bytes")
                self.assertIn(symbol, relocations.stdout,
                              f"{name} is not referenced by game code")


if __name__ == "__main__":
    unittest.main()
