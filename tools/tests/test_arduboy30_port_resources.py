"""On-demand port resource contract and static-RAM evidence; host only.

Compiles the real ports, the real compat layer and ArduboyPortRuntime with the
shared fake device stubs. No embedded build, EEPROM backend or device is used.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name("fixtures") / "arduboy30_port_resources"
STUBS = Path(__file__).with_name("fixtures") / "arduboy_port_runtime"
GAMES = ROOT / "firmware/rtl8720dn/NanoUI/app/apps/games"
APPS = ROOT / "firmware/rtl8720dn/NanoUI/app"

# Ports exercised by resources.cpp: four save ports and four zero-save ports.
RESOURCE_PORTS = [
    "ardubullets",
    "chiemagari",
    "ardugo",
    "galaxion",
    "asteroiddodge",
    "memoryrhythm",
    "pong",
    "longcat",
]

ALL_PORTS = """ardubullets bananonsense chiemagari chribocchi evasion knightmove
ardugo minesweeper game2048 galaxion spacetanks stairssweep toyokumono rysk
reversi quarto asteroiddodge memoryrhythm pong jumpboy longcat""".split()


def native_compiler():
    return shutil.which("g++")


def stage_tree(root, ports):
    (root / "src").mkdir(parents=True)
    shutil.copytree(APPS / "compat", root / "app/compat")
    for game in ports:
        shutil.copytree(GAMES / game, root / "app/apps/games" / game)
    shutil.copy2(STUBS / "NanoUI.h", root / "src/NanoUI.h")
    shutil.copy2(STUBS / "Arduino.h", root / "Arduino.h")
    shutil.copy2(STUBS / "AmebaStore.h", root / "AmebaStore.h")


def compile_args(compiler, root, source, output, link=True):
    args = [compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
            "-Wno-unused-parameter", "-I", str(root),
            "-I", str(ROOT / "libraries/NanoUI/src"), str(source)]
    if link:
        args += ["-o", str(output)]
    else:
        args += ["-c", "-o", str(output)]
    return args


class Arduboy30PortResourceTests(unittest.TestCase):
    def test_all21_states_can_coexist_and_release(self):
        compiler = native_compiler()
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-arduboy-all-live-") as directory:
            root = Path(directory)
            stage_tree(root, ALL_PORTS)
            executable = root / "all_live.exe"
            build = subprocess.run(
                compile_args(compiler, root, FIXTURE / "all_live.cpp", executable),
                capture_output=True, text=True, timeout=240,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True,
                                 timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("all21 live state retention passed", run.stdout)

    def test_resource_contract(self):
        compiler = native_compiler()
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-arduboy-resources-") as directory:
            root = Path(directory)
            stage_tree(root, RESOURCE_PORTS)
            executable = root / "resources.exe"
            build = subprocess.run(
                compile_args(compiler, root, FIXTURE / "resources.cpp", executable),
                capture_output=True, text=True, timeout=180,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True,
                                 timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("arduboy resource contract passed", run.stdout)
            for expected in (
                "duplicate launch refused",
                "allocation failures rolled back",
                "saves persist and stay isolated",
                "zero-save guard, OOB semantics, no hidden slot",
                "repeated switching stays stable",
            ):
                self.assertIn(expected, run.stdout)

    def test_no_static_port_engines(self):
        compiler = native_compiler()
        nm = shutil.which("nm")
        objdump = shutil.which("objdump")
        if not compiler or not nm or not objdump:
            self.skipTest("g++/nm/objdump are unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-arduboy-nm-") as directory:
            root = Path(directory)
            stage_tree(root, ALL_PORTS)
            obj = root / "no_static_engines.o"
            build = subprocess.run(
                compile_args(compiler, root, FIXTURE / "no_static_engines.cpp", obj,
                             link=False),
                capture_output=True, text=True, timeout=240,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)

            # This toolchain's nm omits symbol sizes for PE objects, so take the
            # authoritative section sizes from objdump. A surviving per-port
            # engine/framebuffer would be a >2 KiB .bss/.data blob.
            headers = subprocess.run([objdump, "-h", str(obj)],
                                     capture_output=True, text=True, timeout=60)
            self.assertEqual(headers.returncode, 0, headers.stdout + headers.stderr)
            sections = []
            for line in headers.stdout.splitlines():
                fields = line.split()
                if len(fields) >= 3 and fields[1].startswith((".data", ".bss")):
                    try:
                        size = int(fields[2], 16)
                    except ValueError:
                        continue
                    sections.append((size, fields[1]))
            self.assertTrue(sections, "no .data/.bss sections found")
            largest = max(sections)
            self.assertLess(
                largest[0], 1024,
                f"{largest[1]} is engine-sized: {largest[0]} bytes",
            )
            self.assertLess(sum(item[0] for item in sections), 4096,
                            "static mutable port RAM regressed")
            # Direct name evidence: a surviving engine/global would keep
            # 'arduboy' in its mangled symbol name.
            listing = subprocess.run([nm, "--radix=d", str(obj)],
                                     capture_output=True, text=True, timeout=60)
            self.assertEqual(listing.returncode, 0, listing.stdout + listing.stderr)
            offenders = []
            for line in listing.stdout.splitlines():
                fields = line.split()
                if len(fields) >= 3 and fields[1] in "BbDdGgSs" and "arduboy" in fields[2]:
                    offenders.append(fields[2])
            self.assertFalse(offenders, f"static engine symbols remain: {offenders}")


if __name__ == "__main__":
    unittest.main()
