"""Replay mechanical extraction assertions against a fixed historical Git tree.

No checkout, SDK, worktree mutation or comparison with today's layout bodies.
Missing history is an error, never a skipped or successful historical check.
"""

import re
import subprocess
import unittest

from tools.tests.test_nanoui_original_extraction import (
    BASE,
    PIECES,
    ROOT,
    ROOTS,
    implementation_body,
    integration_paths,
    original,
    original_body,
)


EXTRACTION = "1bd4cdd9f1953130839d5160a16095d95dac3e57"


class HistoricalExtractionTests(unittest.TestCase):
    def test_all_eleven_bodies_match_both_original_ports(self):
        for name in PIECES:
            with self.subTest(piece=name):
                sources = [original(f"{port}/{name}.h") for port in ROOTS]
                self.assertEqual(sources[0], sources[1])
                shared = original(f"libraries/NanoUI/src/NanoUI/product/{name}.h", EXTRACTION)
                self.assertEqual(implementation_body(shared), original_body(name, sources[0]))

    def test_historical_forwarders(self):
        for name in PIECES:
            for port in ROOTS:
                with self.subTest(port=port, piece=name):
                    text = original(f"{port}/{name}.h", EXTRACTION)
                    self.assertIn(f"#include <NanoUI/product/{name}.h>", text)
                    self.assertNotIn("class ", text)

    def test_historical_product_scope(self):
        changed = subprocess.check_output(
            [
                "git",
                "diff",
                "--name-only",
                "--no-renames",
                BASE,
                EXTRACTION,
                "--",
                "firmware",
                "apps",
            ],
            cwd=ROOT,
            text=True,
        ).splitlines()
        unexpected = set(changed) - integration_paths()
        self.assertFalse(unexpected, unexpected)

    def test_historical_core_has_no_sdk_or_reverse_product_includes(self):
        paths = subprocess.check_output(
            ["git", "ls-tree", "-r", "--name-only", EXTRACTION, "--", "libraries/NanoUI/src"],
            cwd=ROOT,
            text=True,
        ).splitlines()
        self.assertTrue(paths)
        for path in paths:
            if not path.endswith((".h", ".cpp")):
                continue
            includes = re.findall(
                r'^\s*#\s*include\s+[<"]([^>"]+)', original(path, EXTRACTION), flags=re.M
            )
            for dependency in includes:
                with self.subTest(path=path, include=dependency):
                    self.assertNotRegex(
                        dependency.lower(),
                        r"arduino|stm32|freertos|zephyr|sdl|tracy|avr/|firmware|app_real|sdk",
                    )


if __name__ == "__main__":
    unittest.main()
