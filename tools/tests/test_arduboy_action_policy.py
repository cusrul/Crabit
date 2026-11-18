"""Host-only checks for the shared PAT action policy and port-id slots.

The fixture compiles the real ArduboyCompatApp/ArduboyPortRuntime with the
same fake NanoUI/Arduino/AmebaStore boundary as the other native adapter tests.
No embedded build, device or hardware claim is made.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name("fixtures") / "arduboy_action_policy"
RUNTIME = Path(__file__).with_name("fixtures") / "arduboy_port_runtime"


class ArduboyActionPolicyTest(unittest.TestCase):
    def test_default_and_swapped_pat_policy(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("native g++ is unavailable")
        with tempfile.TemporaryDirectory(prefix="crabit-arb-policy-") as directory:
            root = Path(directory)
            (root / "app/compat/arduboy/legacy").mkdir(parents=True)
            (root / "src").mkdir()
            source = ROOT / "firmware/rtl8720dn/NanoUI/app/compat/arduboy"
            for name in ("Arduboy2Compat.h", "ArduboyAudioCompat.h", "ArduboyPortRuntime.h", "ArduboyCompatApp.h"):
                shutil.copy2(source / name, root / "app/compat/arduboy" / name)
            for name in ("ArduboyCore.h", "ArduboyApp.h"):
                shutil.copy2(source / "legacy" / name, root / "app/compat/arduboy/legacy" / name)
            shutil.copy2(RUNTIME / "NanoUI.h", root / "src/NanoUI.h")
            shutil.copy2(RUNTIME / "Arduino.h", root / "Arduino.h")
            shutil.copy2(RUNTIME / "AmebaStore.h", root / "AmebaStore.h")
            output = root / "action-policy.exe"
            build = subprocess.run(
                [compiler, "-std=c++11", "-O1", "-Wall", "-Wextra",
                 "-Wno-unused-parameter", "-I", str(root),
                 str(FIXTURE / "runtime.cpp"), "-o", str(output)],
                capture_output=True, text=True, timeout=90,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(output)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("action policy runtime passed", run.stdout)

    def test_live_port_id_count_covers_hopper(self):
        header = (ROOT / "firmware/rtl8720dn/NanoUI/app/compat/arduboy/ArduboyPortRuntime.h")
        text = header.read_text(encoding="utf-8")
        self.assertIn("constexpr uint8_t kPortIdCount = 22;", text)
        app = (ROOT / "firmware/rtl8720dn/NanoUI/app/apps/games/hopper/app.h")
        self.assertIn("kPortId = 21;", app.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
