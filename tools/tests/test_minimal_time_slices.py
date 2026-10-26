import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURE = pathlib.Path(__file__).with_name("minimal_time_slices.cpp")


class MinimalTimeSlicesTests(unittest.TestCase):
    def test_compiles_against_product_nanoui_and_preserves_legacy_pixels(self):
        with tempfile.TemporaryDirectory(prefix="minimal-time-slices-") as temporary:
            executable = pathlib.Path(temporary) / "minimal_time_slices.exe"
            command = [
                "g++",
                "-std=c++11",
                "-DNANOUI_DYNAMIC_STORAGE=1",
                "-I",
                str(ROOT / "libraries/NanoUI/src"),
                str(FIXTURE),
                str(ROOT / "libraries/NanoUI/src/NanoUI/product/runtime.cpp"),
                "-o",
                str(executable),
            ]
            compiled = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("minimal time slices failures=0", result.stdout)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
