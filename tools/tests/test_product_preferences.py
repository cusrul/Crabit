"""Compile and exercise the real shared product-preferences header on host."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProductPreferencesTests(unittest.TestCase):
    def test_real_header_contract_on_host(self):
        compiler = shutil.which("g++") or r"C:\w64dev\w64devkit\bin\g++.exe"
        self.assertTrue(Path(compiler).is_file(), "an existing host g++ is required")

        with tempfile.TemporaryDirectory(prefix="crabit-product-preferences-") as folder:
            binary = Path(folder) / "product_preferences.exe"
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(ROOT / "tools/tests/product_preferences.cpp"),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
