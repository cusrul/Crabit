"""Compile the complete production WS2812 header against bounded host fakes."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PRODUCTION_HEADER = ROOT / "firmware/stm32/Core/Inc/hw_ws2812.h"
FIRMWARE_INCLUDE = ROOT / "firmware/stm32/Core/Inc"
LED_RAIL_HEADER = FIRMWARE_INCLUDE / "led_rail_stop2.h"
CPP_FIXTURE = ROOT / "tools/tests/ws2812_diagnostics.cpp"
FAKE_INCLUDE = ROOT / "tools/tests/ws2812_fakes"
FAKE_TIM = FAKE_INCLUDE / "tim.h"


class Ws2812DiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = next(
            (
                candidate
                for candidate in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++"))
                if candidate and Path(candidate).is_file()
            ),
            None,
        )
        if compiler is None:
            raise unittest.SkipTest("an existing local g++ compiler is required")
        cls.compiler = str(compiler)
        cls.temp_context = tempfile.TemporaryDirectory(prefix="crabit-ws2812-diagnostics-")
        cls.temp_root = Path(cls.temp_context.name).resolve()
        cls.staged_dir = cls.temp_root / "staged-header"
        cls.staged_dir.mkdir()
        cls.staged_header = cls.staged_dir / "hw_ws2812.h"
        shutil.copyfile(PRODUCTION_HEADER, cls.staged_header)

        if not cls.staged_header.resolve().is_relative_to(cls.temp_root):
            raise AssertionError("staged production header escaped its unique temporary directory")
        if cls.staged_header.read_bytes() != PRODUCTION_HEADER.read_bytes():
            raise AssertionError("staging must preserve every production header byte")
        if sorted(path.name for path in cls.staged_dir.iterdir()) != ["hw_ws2812.h"]:
            raise AssertionError("staging may contain only the copied production header")
        if (cls.staged_dir / "tim.h").exists():
            raise AssertionError("staging must not shadow the dedicated fake tim.h")
        if (CPP_FIXTURE.parent / "hw_ws2812.h").exists():
            raise AssertionError("fixture-local header would bypass the bounded staged copy")
        if not FAKE_TIM.is_file() or FAKE_TIM.parent.resolve() != FAKE_INCLUDE.resolve():
            raise AssertionError("expected the dedicated test-only fake tim.h")

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "temp_context"):
            cls.temp_context.cleanup()

    def compile_and_run(self, hardware):
        binary = self.temp_root / ("ws2812_hardware.exe" if hardware else "ws2812_desktop.exe")
        command = [
            self.compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-H",
            "-I",
            str(self.staged_dir),
            "-I",
            str(FAKE_INCLUDE),
            "-I",
            str(FIRMWARE_INCLUDE),
        ]
        if hardware:
            command.extend(("-DUSE_HAL_DRIVER", "-DSTM32L432xx", "-DWS2812_HARDWARE_TEST"))
        command.extend((str(CPP_FIXTURE), "-o", str(binary)))

        build = subprocess.run(
            command,
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        include_trace = build.stderr.replace("\\", "/").casefold()
        self.assertIn(str(self.staged_header).replace("\\", "/").casefold(), include_trace)
        if hardware:
            self.assertIn(str(FAKE_TIM.resolve()).replace("\\", "/").casefold(), include_trace)
            self.assertIn(
                str(LED_RAIL_HEADER.resolve()).replace("\\", "/").casefold(), include_trace
            )

        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5, check=False)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return run.stdout

    def test_full_production_header_with_simulated_running_timer(self):
        output = self.compile_and_run(hardware=True)
        self.assertIn("WS2812 hardware-header diagnostics tests PASS", output)

    def test_desktop_software_only_api_without_hal(self):
        output = self.compile_and_run(hardware=False)
        self.assertIn("WS2812 desktop software-only tests PASS", output)


if __name__ == "__main__":
    unittest.main()
