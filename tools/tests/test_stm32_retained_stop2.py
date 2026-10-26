"""Run the retained STOP2 header fixtures and real-stack USB warm-init regression.

The C++ fixtures compile against normal firmware/stm32/Core/Inc headers. The C
fixture links the production MX_USB_DEVICE_Init entry and ST USB stack, with
only HAL/USB-LL behavior represented by host fakes.
"""

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STM = ROOT / "firmware/stm32"
CORE_INC = STM / "Core/Inc"
TESTS = Path(__file__).with_name("retained_stop2")

CPP_FIXTURES = (
    "wake_diag_format_test.cpp",
    "i2c_sample_trace_test.cpp",
    "i2c_recovery_test.cpp",
    "aux_stop2_test.cpp",
    "qmi_wake_trace_test.cpp",
    "qmi_run_profile_test.cpp",
    "qmi_run_probe_test.cpp",
    "sensor_stop2_test.cpp",
    "led_rail_stop2_test.cpp",
    "idle_sleep_policy_test.cpp",
    "mcu_stop2_sequence_test.cpp",
)
USB_CASES = (
    "actual_mx",
    "descriptor",
    "routing",
    "keyboard_completion",
    "led_out",
    "flash_rx",
    "reset_cycles",
    "allocation_failure",
    "flash_queue_sof",
    "suspend",
    "get_report",
)

USB = STM / "USB_DEVICE"
LIB = STM / "Middlewares/ST/STM32_USB_Device_Library"
FAKES = Path(__file__).with_name("usb_composite_fakes")
USB_SOURCES = (
    USB / "App/usb_device.c",
    USB / "App/usbd_cdc_if.c",
    USB / "Composite/usb_composite.c",
    USB / "Composite/usb_keyboard.c",
    USB / "Composite/usb_diag.c",
    LIB / "Core/Src/usbd_core.c",
    LIB / "Core/Src/usbd_ctlreq.c",
    LIB / "Core/Src/usbd_ioreq.c",
    LIB / "Class/CompositeBuilder/Src/usbd_composite_builder.c",
    LIB / "Class/CDC/Src/usbd_cdc.c",
    LIB / "Class/CustomHID/Src/usbd_customhid.c",
)
USB_INCLUDE_DIRS = (
    FAKES,
    USB / "Composite",
    USB / "App",
    STM / "Core/Inc",
    LIB / "Core/Inc",
    LIB / "Class/CustomHID/Inc",
    LIB / "Class/CDC/Inc",
    LIB / "Class/CompositeBuilder/Inc",
)
USB_FIXTURE = TESTS / "usb_mx_reinit_test.c"
USB_DECL = TESTS / "usb_mx_test_decl.h"


def _find_compiler(environment_name, executable, fallback):
    """Prefer an explicitly configured compiler, then PATH, then project fallback."""
    configured = os.environ.get(environment_name, "").strip().strip('"')
    candidates = []
    if configured:
        resolved = shutil.which(configured)
        if resolved:
            candidates.append(Path(resolved))
        candidates.append(Path(configured))
    resolved = shutil.which(executable)
    if resolved:
        candidates.append(Path(resolved))
    candidates.append(Path(fallback))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def _snapshot(paths):
    return {path: hashlib.sha256(path.read_bytes()).digest() for path in paths}


def _assert_snapshot(snapshot):
    for path, digest in snapshot.items():
        if not path.is_file():
            raise AssertionError(f"source disappeared during fixture: {path}")
        if hashlib.sha256(path.read_bytes()).digest() != digest:
            raise AssertionError(
                "source changed during fixture; rerun against one firmware snapshot: " + str(path)
            )


def _run(command, cwd, timeout=60):
    return subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)


class RetainedStop2HeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = _find_compiler("CXX", "g++", "C:/w64dev/w64devkit/bin/g++.exe")
        if compiler is None:
            raise unittest.SkipTest(
                "native g++ unavailable (checked CXX, PATH, and the project fallback)"
            )

        cls._temp = tempfile.TemporaryDirectory(prefix="crabit-retained-stop2-cpp-")
        cls.addClassCleanup(cls._temp.cleanup)
        temp = Path(cls._temp.name)
        cls.binaries = {}
        fixture_sources = [TESTS / name for name in CPP_FIXTURES]
        for source in fixture_sources:
            if not source.is_file():
                raise AssertionError(f"retained STOP2 fixture missing: {source}")
            quoted = re.findall(
                r'^\s*#include\s*"([^"]+)"',
                source.read_text(encoding="utf-8"),
                flags=re.MULTILINE,
            )
            missing = [CORE_INC / name for name in quoted if not (CORE_INC / name).is_file()]
            if missing:
                raise AssertionError(
                    "normal firmware headers are not promoted yet; missing: "
                    + ", ".join(str(path) for path in missing)
                )

        headers = tuple(CORE_INC.glob("*.h"))
        cls.source_snapshot = _snapshot([*fixture_sources, *headers])
        for source in fixture_sources:
            binary = temp / (source.stem + (".exe" if os.name == "nt" else ""))
            command = [
                str(compiler),
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O0",
                "-g",
                "-I",
                str(CORE_INC),
                str(source),
                "-o",
                str(binary),
            ]
            result = _run(command, ROOT)
            if result.returncode:
                raise AssertionError(
                    f"C++ fixture compile failed ({source.name}):\n" + result.stdout + result.stderr
                )
            cls.binaries[source.name] = binary
        _assert_snapshot(cls.source_snapshot)

    def run_fixture(self, name):
        _assert_snapshot(self.source_snapshot)
        result = _run([str(self.binaries[name])], ROOT, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class RetainedStop2UsbRealStackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name != "nt":
            raise unittest.SkipTest(
                "real-stack fixture needs Windows PE low-address mapping for ST 32-bit pointer casts"
            )
        compiler = _find_compiler("CC", "gcc", "C:/msys64/ucrt64/bin/gcc.exe")
        if compiler is None:
            raise unittest.SkipTest(
                "native gcc unavailable (checked CC, PATH, and the project fallback)"
            )

        missing = [path for path in (*USB_SOURCES, USB_FIXTURE, USB_DECL) if not path.is_file()]
        missing.extend(path for path in USB_INCLUDE_DIRS if not path.is_dir())
        target_conf = USB / "Target/usbd_conf.h"
        if not target_conf.is_file():
            missing.append(target_conf)
        if missing:
            raise AssertionError(
                "production USB source/helper is unavailable: "
                + ", ".join(str(path) for path in missing)
            )

        cls._temp = tempfile.TemporaryDirectory(prefix="crabit-retained-stop2-usb-")
        cls.addClassCleanup(cls._temp.cleanup)
        cls.binary = Path(cls._temp.name) / "usb-mx-real-stack.exe"
        monitored = [*USB_SOURCES, USB_FIXTURE, USB_DECL, target_conf]
        for directory in USB_INCLUDE_DIRS:
            monitored.extend(directory.glob("*.h"))
        cls.source_snapshot = _snapshot(set(monitored))

        command = [
            str(compiler),
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O0",
            "-g",
            "-iquote",
            str(USB / "Target"),
            "-Wno-pointer-to-int-cast",
            "-Wno-int-to-pointer-cast",
            "-Wl,--image-base,0x400000,--disable-dynamicbase",
        ]
        for directory in USB_INCLUDE_DIRS:
            command.extend(["-I", str(directory)])
        command.extend(["-include", str(USB_DECL)])
        command.extend([str(USB_FIXTURE), *(str(path) for path in USB_SOURCES)])
        command.extend(["-o", str(cls.binary)])
        result = _run(command, ROOT)
        if result.returncode:
            raise AssertionError(
                "real-stack USB fixture compile failed:\n" + result.stdout + result.stderr
            )
        _assert_snapshot(cls.source_snapshot)

    def run_case(self, name):
        _assert_snapshot(self.source_snapshot)
        result = _run([str(self.binary), name], ROOT, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS {name}", result.stdout)


def _cpp_test(fixture):
    def test(self):
        self.run_fixture(fixture)

    return test


for _fixture in CPP_FIXTURES:
    _name = Path(_fixture).stem.removesuffix("_test")
    setattr(RetainedStop2HeaderTests, f"test_cpp_{_name}", _cpp_test(_fixture))


def _usb_test(case):
    def test(self):
        self.run_case(case)

    return test


for _case in USB_CASES:
    test_name = (
        "test_usb_actual_mx_reinit_40_cycles" if _case == "actual_mx" else f"test_usb_{_case}"
    )
    setattr(RetainedStop2UsbRealStackTests, test_name, _usb_test(_case))


if __name__ == "__main__":
    unittest.main()
