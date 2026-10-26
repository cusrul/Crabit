"""Negative probes: reclassifying old tests must not silently remove protection."""

from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

from tools.tests import test_nanoui_original_extraction as current
from tools.tests import test_nanoui_extraction_history as history


class GuardRegressionTests(unittest.TestCase):
    def run_guard(self, case, method):
        result = unittest.TestResult()
        case(method).run(result)
        return result

    def test_historical_scope_rejects_out_of_scope_hardware_or_apps(self):
        for path in (
            "firmware/stm32/Core/Inc/sys_power.h",
            "firmware/rtl8720dn/NanoUI/RadioSvc.cpp",
            "firmware/stm32/Core/NanoUI/app_real/new_page.h",
            "apps/mobile/lib/core/app_state.dart",
            "apps/mobile/lib/core/unapproved_service.dart",
        ):
            with self.subTest(path=path):

                def output(argv, **kwargs):
                    self.assertEqual(argv[1], "diff")
                    self.assertIn(current.INTEGRATION_SNAPSHOT, argv)
                    return path + "\n"

                with patch.object(current.subprocess, "check_output", output):
                    result = self.run_guard(
                        current.OriginalExtractionTests,
                        "test_historical_framework_integration_scope",
                    )
                self.assertTrue(result.failures)
                self.assertFalse(result.errors or result.skipped)

    def test_normalization_preserves_literal_spaces_and_actual_edits(self):
        normalize = current.implementation_body
        self.assertEqual(normalize('const char* p = "A B";'), normalize('const char*p="A B";'))
        self.assertNotEqual(normalize('const char*p="A B";'), normalize('const char*p="AB";'))
        self.assertNotEqual(normalize("int a=1;"), normalize("int a=2;"))

    def test_visual_scope_is_exact_and_keeps_device_state_protected(self):
        allowed = current.integration_phase_paths()
        self.assertLessEqual(current.MOBILE_VISUAL_PATHS, allowed)
        self.assertIn("apps/mobile/lib/main.dart", current.MOBILE_VISUAL_PATHS)
        self.assertNotIn("apps/mobile/lib/core/app_state.dart", allowed)

    def test_unchanged_bodies_are_still_protected(self):
        read = Path.read_text
        for piece in current.UNCHANGED_PIECES:
            with self.subTest(piece=piece):
                protected = current.ROOT / f"libraries/NanoUI/src/NanoUI/product/{piece}.h"

                def changed(path, *args, **kwargs):
                    text = read(path, *args, **kwargs)
                    return text + "\n// unauthorized edit\n" if path == protected else text

                with patch.object(Path, "read_text", changed):
                    result = self.run_guard(
                        current.OriginalExtractionTests,
                        "test_unchanged_bodies_are_original_on_both_ports",
                    )
                self.assertTrue(result.failures)
                self.assertFalse(result.errors or result.skipped)

    def test_sdk_include_is_still_rejected(self):
        read = Path.read_text
        entry = current.ROOT / "libraries/NanoUI/src/NanoUI.h"

        def changed(path, *args, **kwargs):
            text = read(path, *args, **kwargs)
            return text + "\n#include <Arduino.h>\n" if path == entry else text

        with patch.object(Path, "read_text", changed):
            result = self.run_guard(
                current.OriginalExtractionTests,
                "test_portable_source_has_no_sdk_or_reverse_product_includes",
            )
        self.assertTrue(result.failures)
        self.assertFalse(result.errors or result.skipped)

    def test_history_does_not_read_current_layout_files(self):
        with patch.object(Path, "read_text", side_effect=AssertionError("current tree read")):
            result = self.run_guard(
                history.HistoricalExtractionTests,
                "test_all_eleven_bodies_match_both_original_ports",
            )
        self.assertTrue(result.wasSuccessful())

    def test_missing_history_is_an_error_not_a_skip(self):
        error = subprocess.CalledProcessError(128, ["git", "show"])
        with patch.object(history, "original", side_effect=error):
            result = self.run_guard(
                history.HistoricalExtractionTests,
                "test_all_eleven_bodies_match_both_original_ports",
            )
        self.assertTrue(result.errors)
        self.assertFalse(result.skipped)


if __name__ == "__main__":
    unittest.main()
