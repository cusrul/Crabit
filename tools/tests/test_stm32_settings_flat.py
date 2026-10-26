"""Host execution tests for the real flat STM32 SettingsApp.

The C++ fixture includes the product App/configuration headers and shared
NanoUI implementation. Only MCU-facing HAL calls are replaced by spies.
"""

from __future__ import annotations

import os
import hashlib
import struct
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "firmware/stm32/Core/NanoUI"
SHARED = ROOT / "libraries/NanoUI"
FIXTURE = Path(__file__).resolve().parent / "stm32_settings_flat.cpp"
FAKE = Path(__file__).resolve().parent / "settings_flat_fakes/fake_hal.h"


def compiler() -> str | None:
    configured = os.environ.get("CXX")
    if configured:
        return shutil.which(configured) or (configured if Path(configured).is_file() else None)
    return shutil.which("g++") or shutil.which("x86_64-w64-mingw32-g++") or shutil.which("g++-14")


class FlatSettingsHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cxx = compiler()
        if not cls.cxx:
            raise unittest.SkipTest(
                "No existing g++/w64 compiler found; toolchain installation is out of scope"
            )
        missing = [
            str(path)
            for path in (
                FIXTURE,
                FAKE,
                UI / "app_real/app_settings.h",
                UI / "app_real/components/settings_option_picker.h",
                ROOT / "firmware/stm32/Core/Inc/sys_config.h",
            )
            if not path.is_file()
        ]
        if missing:
            raise AssertionError("Required host-test inputs are missing: " + ", ".join(missing))
        cls.temp = tempfile.TemporaryDirectory(prefix="crabit-settings-flat-")
        cls.exe = Path(cls.temp.name) / "settings-flat-host-test.exe"
        sources = [
            FIXTURE,
            UI / "src/font_engine.cpp",
            SHARED / "src/NanoUI/product/runtime.cpp",
            UI / "src/nanoui_assets.cpp",
        ]
        command = [
            cls.cxx,
            "-std=c++17",
            "-O0",
            "-g",
            "-include",
            str(FAKE),
            "-I",
            str(UI / "include"),
            "-I",
            str(UI / "assets/generated/desktop"),
            "-I",
            str(UI / "app_real"),
            "-I",
            str(ROOT / "firmware/stm32/Core/Inc"),
            "-I",
            str(SHARED / "src"),
            *map(str, sources),
            "-o",
            str(cls.exe),
        ]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if result.returncode:
            raise AssertionError("host fixture compile failed\n" + result.stdout + result.stderr)
        cls.build_command = command

    @classmethod
    def tearDownClass(cls) -> None:
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def test_real_settings_app_contract(self) -> None:
        snapshots = os.environ.get("CRABIT_SETTINGS_SNAPSHOT_DIR")
        if snapshots:
            Path(snapshots).mkdir(parents=True, exist_ok=True)
        result = subprocess.run(
            [str(self.exe), snapshots or ""], cwd=ROOT, text=True, capture_output=True
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout, end="")
        if snapshots:
            expected = (
                "initial",
                "brightness_editing",
                "brightness_editing_settled",
                "off_editing",
                "input_editing",
                "actions_bottom",
                "dirty_initial",
                "bluetooth_on",
            )
            for name in expected:
                image = Path(snapshots) / (name + ".bmp")
                self.assertTrue(image.is_file(), f"missing requested frame snapshot: {image}")
                self.assertEqual(image.stat().st_size, 393270, f"unexpected BMP size: {image}")
                bmp = image.read_bytes()
                self.assertEqual(struct.unpack_from("<ii", bmp, 18), (512, 256))
                self.assertEqual(struct.unpack_from("<H", bmp, 28)[0], 24)
                print(f"snapshot {image.name} sha256={hashlib.sha256(bmp).hexdigest()}")

    def test_current_product_source_shape(self) -> None:
        app = (UI / "app_real/app_settings.h").read_bytes()
        picker = (UI / "app_real/components/settings_option_picker.h").read_bytes()
        config = (ROOT / "firmware/stm32/Core/Inc/sys_config.h").read_bytes()
        app_text = app.decode("utf-8")
        picker_text = picker.decode("ascii")
        self.assertEqual(app_text.count("class SettingsApp"), 1)
        for legacy in ("DisplayConfApp", "PowerConfApp", "GestureConfApp", "ConnConfApp"):
            self.assertNotIn(legacy, app_text)
        for forbidden in ("EventBus", "FreeRTOS.h", "task.h", "app_coproc.h", "BW16_"):
            self.assertNotIn(forbidden, app_text)
        self.assertEqual(app_text.count("ConfigManager::save()"), 1)
        self.assertEqual(app_text.count("new SettingsOptionPicker("), 2)
        self.assertIn("new Slider(brightness, 1.0f, 28", app_text)
        self.assertIn("new Container(new SlidingText(&title, 8), 128, 12)", app_text)
        self.assertIn('"%d%%"', app_text)
        self.assertIn('"自定义"', app_text)
        self.assertIn("off_times[] = {3000, 5000, 10000, 15000, 30000}", app_text)
        self.assertIn("bool deep_sleep_enabled = true;", app_text)
        self.assertIn("uint32_t sleepDelay() const", app_text)
        self.assertIn("deep_sleep_enabled = original_sleep != 0;", app_text)
        self.assertRegex(app_text, r'settingRow\("深睡",\s*new Switch\(deep_sleep_enabled,')
        self.assertNotIn("sleep_times[]", app_text)
        self.assertIn(
            "static constexpr int off_custom = sizeof(off_times) / sizeof(off_times[0])", app_text
        )
        self.assertIn('off_labels = {"3s", "5s", "10s", "15s", "30s"};', app_text)
        self.assertNotIn("sleep_labels", app_text)
        self.assertNotIn('new Container(new Text("当前仅息屏 不深睡", 8), 128, 10)', app_text)
        self.assertIn("class SettingsOptionPicker", picker_text)
        self.assertIn("class RenderSettingsOptionPicker", picker_text)
        self.assertIn("CONFIG_FLASH_ADDR 0x0803F800u", config.decode("latin1"))
        for name, data in (
            ("app_settings.h", app),
            ("settings_option_picker.h", picker),
            ("sys_config.h", config),
        ):
            print(f"source {name} sha256={hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
