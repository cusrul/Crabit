"""Architecture guard: wire topics have one source of truth."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"
TOPIC_FILE = FIRMWARE / "shared/topics.h"
TOPIC_RE = re.compile(r'(?<!\\)"((?:sys|app|net|diag|sns|audit|mode)/[^"\n]*)"')
SKIP_DIRS = {"tests", "build", "libraries", "assets"}
SKIP_FILES = {
    "topics.h",
    "crabit_wire.h",
    "crabit_mode.h",
    "audit_contract.h",
    "settings_contract.h",
    "product_preferences.h",
    "companion_probe.h",
    "weather_contract.h",
}


class TopicBoundaryTests(unittest.TestCase):
    def test_product_code_has_no_raw_topic_literals(self):
        offenders = []
        for root in (
            FIRMWARE / "stm32/Core",
            FIRMWARE / "stm32/product",
            FIRMWARE / "rtl8720dn/NanoUI",
        ):
            for path in root.rglob("*"):
                if path.suffix not in (".h", ".cpp"):
                    continue
                if any(part in SKIP_DIRS for part in path.parts):
                    continue
                if path.name in SKIP_FILES:
                    continue
                text = path.read_text(encoding="utf-8", errors="ignore")
                for line_number, line in enumerate(text.splitlines(), 1):
                    if line.lstrip().startswith("//"):
                        continue
                    for match in TOPIC_RE.findall(line):
                        offenders.append(f"{path.relative_to(ROOT)}:{line_number}: {match}")
        self.assertEqual(offenders, [], "\n".join(offenders))

    def test_contract_headers_reference_topics(self):
        for name in (
            "settings_contract.h",
            "product_preferences.h",
            "companion_probe.h",
            "audit_contract.h",
            "crabit_mode.h",
        ):
            text = (FIRMWARE / "shared" / name).read_text(encoding="utf-8")
            self.assertIn("Topics::", text, f"{name} must use topics.h")


if __name__ == "__main__":
    unittest.main()
