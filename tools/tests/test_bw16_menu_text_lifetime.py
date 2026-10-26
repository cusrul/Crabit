"""Menu labels survive the factory; prove the probe detects the old failure too."""

import shutil
import re
import unittest

from tools.diagnose_bw16_menu_text_lifetime import ROOT, run_probe
from tools.render_bw16_small_fonts import block


@unittest.skipUnless(shutil.which("g++"), "Host g++ is required")
class MenuTextLifetimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "firmware/rtl8720dn/NanoUI/app/launcher.h").read_text(encoding="utf-8")
        cls.factory = block(source, "inline Widget* buildModernAnimMenu(")

    def test_labels_outlive_factory(self):
        output = run_probe(self.factory, ROOT / "build/menu-text-lifetime-regression/current")
        self.assertIn("live labels in Widget/RenderText: 2/2 2/2", output)

    def test_all_folder_callers_use_literal_pointer_containers(self):
        source = (ROOT / "firmware/rtl8720dn/NanoUI/app/launcher.h").read_text(encoding="utf-8")
        # The primary menu owns std::string members; only fixed folder factories
        # need borrowed literal pointers. Cover inactive profile branches too.
        folders = source.split("class BleFolderApp", 1)[1].split("class LauncherApp", 1)[0]
        self.assertNotIn("std::vector<std::string>{", folders)

    def test_probe_detects_temporary_string_regression(self):
        unsafe = self.factory.replace(
            "std::vector<const char*> titles", "std::vector<std::string> titles"
        )
        unsafe, substitutions = re.subn(
            r"new Text\(titles\[i\], (\d+)\)", r"new Text(titles[i].c_str(), \1)", unsafe
        )
        self.assertEqual(substitutions, 1)
        self.assertNotEqual(unsafe, self.factory)
        output = run_probe(
            unsafe, ROOT / "build/menu-text-lifetime-regression/negative", expect_dangling=True
        )
        self.assertIn("Widget pointers into released storage: 2", output)
        self.assertIn("RenderText pointers into released storage: 2", output)


if __name__ == "__main__":
    unittest.main()
