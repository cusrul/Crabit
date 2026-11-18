"""Compile and run the shared command/feature registry contract on the host."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AppInterfaceTests(unittest.TestCase):
    def test_command_and_feature_registries(self):
        compiler = shutil.which("g++") or r"C:\w64dev\w64devkit\bin\g++.exe"
        self.assertTrue(Path(compiler).is_file(), "an existing host g++ is required")

        with tempfile.TemporaryDirectory(prefix="crabit-app-interfaces-") as folder:
            binary = Path(folder) / "app_interfaces.exe"
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(ROOT / "tools/tests/app_interfaces_test.cpp"),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
