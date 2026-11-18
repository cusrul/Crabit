"""Guard the STM32 input driver against concrete UI-app dependencies."""

from pathlib import Path
import re
import unittest

from tools.tests.test_bw16_uart_ingress import extract_function

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "firmware/stm32/Core"
HEADER = CORE / "Inc/hw_input.h"
INCLUDE_ROOTS = (
    CORE / "Inc",
    CORE / "NanoUI/include",
    CORE / "NanoUI",
    ROOT / "firmware/shared",
)
CONCRETE_UI = ("app_real", "ble_controls.h", "toggleBleFromUi", "CompanionBle", "Popup", "AppManager")


def local_includes(path):
    text = path.read_text(encoding="utf-8", errors="ignore")
    for match in re.finditer(r'^\s*#include\s+"([^"]+)"', text, re.MULTILINE):
        name = match.group(1)
        candidate = (path.parent / name).resolve()
        if candidate.is_file():
            yield candidate
            continue
        for root in INCLUDE_ROOTS:
            candidate = (root / name).resolve()
            if candidate.is_file():
                yield candidate
                break


class InputBoundaryTests(unittest.TestCase):
    def test_header_and_include_closure_have_no_concrete_ui_layer(self):
        offenders = []
        seen = set()
        pending = [HEADER.resolve()]
        while pending:
            path = pending.pop()
            if path in seen:
                continue
            seen.add(path)
            text = path.read_text(encoding="utf-8", errors="ignore")
            if path == HEADER.resolve():
                for token in CONCRETE_UI:
                    if token in text:
                        offenders.append(f"{path.relative_to(ROOT)} contains {token}")
            for included in local_includes(path):
                relative = included.relative_to(ROOT)
                if "app_real" in relative.parts or included.name == "ble_controls.h":
                    offenders.append(f"{path.relative_to(ROOT)} includes {relative}")
                pending.append(included)
        self.assertEqual(offenders, [], "\n".join(offenders))

    def test_shortcut_hook_is_a_null_default_ui_task_callback(self):
        source = HEADER.read_text(encoding="utf-8")
        self.assertRegex(source, r"using\s+ShortcutHandler\s*=\s*void\s*\(\s*\*\s*\)\s*\(\s*\)\s*;")
        self.assertRegex(source, r"void\s+setBleShortcutHandler\s*\(\s*ShortcutHandler\s+handler\s*\)")
        self.assertRegex(source, r"ShortcutHandler\s+ble_shortcut_handler\s*=\s*nullptr\s*;")
        # init()/resetAfterWake() may re-read pins but must keep the injected hook.
        for signature in ("void resetAfterWake()", "void init() override"):
            body = extract_function(source, signature)
            self.assertNotIn("ble_shortcut_handler", body, signature)


if __name__ == "__main__":
    unittest.main()
