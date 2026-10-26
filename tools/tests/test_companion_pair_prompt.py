"""Exercise product prompt through actual shared NanoUI; no MCU/SDK inputs."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHARED = ROOT / "libraries/NanoUI"
PRODUCT = ROOT / "firmware/stm32/Core/NanoUI/app_real"
CXX = Path(r"C:\w64dev\w64devkit\bin\g++.exe")


class CompanionPairPromptTests(unittest.TestCase):
    def test_actual_framework_ownership_callbacks_and_reuse(self):
        self.assertTrue(CXX.is_file(), "existing host g++ required")
        with tempfile.TemporaryDirectory(prefix="crabit-pair-prompt-") as tmp:
            binary = Path(tmp) / "pair-prompt.exe"
            build = subprocess.run(
                [
                    str(CXX),
                    "-std=c++17",
                    "-O0",
                    "-g",
                    "-Wall",
                    "-Wextra",
                    "-I",
                    str(SHARED / "src"),
                    "-I",
                    str(PRODUCT),
                    str(ROOT / "tools/tests/companion_pair_prompt.cpp"),
                    str(SHARED / "src/NanoUI/product/runtime.cpp"),
                    str(SHARED / "tests/font_stub.cpp"),
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary)], cwd=ROOT, capture_output=True, text=True, timeout=60
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("1000 cycles", run.stdout)
            self.assertIn("zero live heap blocks after teardown", run.stdout)
            print(run.stdout, end="")


if __name__ == "__main__":
    unittest.main(verbosity=2)
