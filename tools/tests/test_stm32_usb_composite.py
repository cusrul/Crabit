"""Compile actual USB prototype C with ST types and asynchronous transport fakes.

No hardware, ARM toolchain, or serial dependency. A second fixture executes the
unmodified descriptor builder at a checked low address in a Windows executable.
Both fixtures drive and observe production code rather than copying algorithms.
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
USB = STM / "USB_DEVICE"
LIB = STM / "Middlewares/ST/STM32_USB_Device_Library"
FAKES = Path(__file__).with_name("usb_composite_fakes")
CASES = (
    "idle",
    "sequence",
    "send_busy",
    "deadline",
    "tick_wrap",
    "flash_sof",
    "reset_reinit",
    "led_rearm",
    "diag_fragments",
    "diag_malformed",
    "diag_overflow",
    "diag_type_busy",
    "diag_cdc_busy",
    "diag_cdc_fail",
    "diag_reset",
    "diag_reset_race",
    "diag_flash",
    "flash_queue_sof",
    "suspend",
    "get_report",
    "allocator",
    "registration",
    "script_sequence",
    "script_cancel",
    "script_limits",
)


class _NativeFixture(unittest.TestCase):
    real_stack = False

    @classmethod
    def setUpClass(cls):
        # Prefer the requested existing compiler. Some installations have removed
        # gcc.exe while leaving a nonfunctional cc.exe alias; do not install/repair.
        candidates = [Path("C:/w64dev/w64devkit/bin/gcc.exe"), Path("C:/msys64/ucrt64/bin/gcc.exe")]
        found = shutil.which("gcc")
        if found:
            candidates.append(Path(found))
        compiler = next((p for p in candidates if p.is_file()), None)
        if compiler is None:
            raise AssertionError("existing native GCC unavailable; host fixture was not run")
        cls.temp = tempfile.TemporaryDirectory(prefix="crabit-usb-composite-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "usb-composite-host.exe"
        sources = [
            USB / "Composite" / name for name in ("usb_keyboard.c", "usb_diag.c", "usb_composite.c")
        ]
        if cls.real_stack:
            sources += [
                LIB / "Core/Src" / name for name in ("usbd_core.c", "usbd_ctlreq.c", "usbd_ioreq.c")
            ]
            sources += [
                LIB / "Class" / name
                for name in (
                    "CompositeBuilder/Src/usbd_composite_builder.c",
                    "CDC/Src/usbd_cdc.c",
                    "CustomHID/Src/usbd_customhid.c",
                )
            ]
            sources.append(USB / "App/usbd_cdc_if.c")
        for path in sources:
            if not path.is_file():
                raise AssertionError(f"production source not available: {path}")
        include_dirs = [
            FAKES,
            USB / "Composite",
            USB / "App",
            LIB / "Core/Inc",
            LIB / "Class/CustomHID/Inc",
            LIB / "Class/CDC/Inc",
            LIB / "Class/CompositeBuilder/Inc",
        ]
        monitored = set(sources)
        for directory in include_dirs:
            monitored.update(directory.glob("*.h"))
        if cls.real_stack:
            monitored.add(USB / "Target/usbd_conf.h")
        cls.source_hashes = {
            str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in monitored
        }
        command = [str(compiler), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O0", "-g"]
        if cls.real_stack:
            # Quoted include resolution selects the actual target conf before
            # our first fixture's fake conf; main/MCU/HAL remain fake.
            command += [
                "-iquote",
                str(USB / "Target"),
                "-Wno-pointer-to-int-cast",
                "-Wno-int-to-pointer-cast",
                "-Wl,--image-base,0x400000,--disable-dynamicbase",
            ]
        for directory in include_dirs:
            command.extend(["-I", str(directory)])
        command += [
            str(FAKES / ("real_stack.c" if cls.real_stack else "fixture.c")),
            *(str(p) for p in sources),
            "-o",
            str(cls.binary),
        ]
        cls.environment = os.environ.copy()
        cls.environment["PATH"] = (
            str(compiler.parent) + os.pathsep + cls.environment.get("PATH", "")
        )
        result = subprocess.run(
            command, cwd=ROOT, env=cls.environment, capture_output=True, text=True, timeout=60
        )
        if result.returncode:
            raise AssertionError("native C compile failed:\n" + result.stdout + result.stderr)
        # A shared checkout can change under a compiler. Never report that mixture
        # as a tested production source snapshot.
        for path, digest in cls.source_hashes.items():
            if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest:
                raise AssertionError("production source changed during native compilation; rerun")

    def run_case(self, name):
        for path, expected in self.source_hashes.items():
            self.assertEqual(
                hashlib.sha256((ROOT / path).read_bytes()).hexdigest(),
                expected,
                "production source changed since fixture compilation; rerun",
            )
        result = subprocess.run(
            [str(self.binary), name],
            cwd=ROOT,
            env=self.environment,
            capture_output=True,
            text=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS {name}", result.stdout)


def _case(name):
    def test(self):
        self.run_case(name)

    return test


class STM32UsbCompositeTests(_NativeFixture):
    pass


class STM32UsbRealStackTests(_NativeFixture):
    real_stack = True


for _name in CASES:
    setattr(STM32UsbCompositeTests, "test_" + _name, _case(_name))

for _name in (
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
):
    setattr(STM32UsbRealStackTests, "test_" + _name, _case(_name))


def _function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for end in range(brace, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return text[start : end + 1]
    raise AssertionError("unterminated production function")


class STM32UsbSourceGuards(unittest.TestCase):
    """Source-only checks for target code that the fake LL cannot execute."""

    def test_target_pma_and_sof_wiring(self):
        target = (USB / "Target/usbd_conf.c").read_text(encoding="utf-8")
        source = re.sub(r"/\*.*?\*/|//[^\n]*", "", target, flags=re.S)
        pairs = re.findall(
            r"HAL_PCDEx_PMAConfig\([^;]+?,\s*(\w+)\s*,\s*PCD_SNG_BUF\s*,\s*(\w+)\s*\)", source
        )
        self.assertCountEqual(
            pairs,
            [
                ("0x00", "CRABIT_USB_PMA_EP0_OUT"),
                ("0x80", "CRABIT_USB_PMA_EP0_IN"),
                ("CRABIT_USB_CDC_IN_EP", "CRABIT_USB_PMA_CDC_IN"),
                ("CRABIT_USB_CDC_OUT_EP", "CRABIT_USB_PMA_CDC_OUT"),
                ("CRABIT_USB_CDC_CMD_EP", "CRABIT_USB_PMA_CDC_CMD"),
                ("CRABIT_USB_HID_IN_EP", "CRABIT_USB_PMA_HID_IN"),
                ("CRABIT_USB_HID_OUT_EP", "CRABIT_USB_PMA_HID_OUT"),
            ],
        )
        self.assertRegex(source, r"\.Init\.Sof_enable\s*=\s*ENABLE\s*;")
        self.assertIn("USBD_LL_SOF", _function(source, "void HAL_PCD_SOFCallback"))

    def test_target_suspend_invalidates_before_stack_suspend(self):
        target = (USB / "Target/usbd_conf.c").read_text(encoding="utf-8")
        body = _function(target, "void HAL_PCD_SuspendCallback")
        self.assertLess(
            body.index("CrabitUsb_DiagReset();"), body.index("CrabitUsb_KeyboardSuspend();")
        )
        self.assertLess(body.index("CrabitUsb_KeyboardSuspend();"), body.index("USBD_LL_Suspend("))

    def test_protected_hardware_headers_remain_at_recovery_snapshot(self):
        # Captured while Lead confirmed these protected files were unchanged.
        # Normalize checkout newlines and UTF-8 BOM. This pins subsequent changes; historical
        # Git comparison remains Lead-owned and is not performed by this test.
        snapshots = {
            "Core/NanoUI/app_real/app_coproc.h": "fb9fda2bd05066ac8031202c586d706ea288b812dede3fdb839fc1bdf5369a06",
        }
        for name, expected in snapshots.items():
            with self.subTest(path=name):
                text = (STM / name).read_text(encoding="utf-8-sig")
                self.assertEqual(hashlib.sha256(text.encode("utf-8")).hexdigest(), expected)
        # The later verified UI localization changed labels/layout, not bridge
        # lifetime. Protect the actual non-render callbacks rather than old UI text.
        relative = "firmware/stm32/Core/NanoUI/app_real/app_bw16_flash.h"
        baseline = subprocess.check_output(["git", "show", f"9ccef62:{relative}"], cwd=ROOT).decode(
            "utf-8-sig"
        )
        current = (ROOT / relative).read_text(encoding="utf-8-sig")

        def logic(text):
            text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
            return re.sub(
                r"\s+", "", text.replace('"READY"', '"就绪"').replace('"Ready"', '"就绪"')
            )

        for signature in ("void onCreate()", "void onDestroy()", "void onTick()"):
            self.assertEqual(
                logic(_function(current, signature)), logic(_function(baseline, signature))
            )
        self.assertEqual(current.count("BW16_Dedicated_Bridge();"), 2)  # declaration + UI action
        helper = _function(
            (STM / "Core/Src/nanoui_port.cpp").read_text(encoding="utf-8"),
            'extern "C" bool STM32_PrepareAmebaUartRx(void)',
        )
        self.assertEqual(
            hashlib.sha256(helper.encode("utf-8")).hexdigest(),
            "25cafb185da46a7eb8f26959a9980c38576cb3a9d4400de342ed199ba3513be9",
        )


if __name__ == "__main__":
    unittest.main()
