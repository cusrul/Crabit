"""Keep the new public entry and existing forwarding paths compatible.

Board umbrella headers intentionally remain product-owned. This is not the old
mechanical extraction identity test and does not rewrite its historical scope.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "libraries/NanoUI/src"
PORTS = (
    ROOT / "firmware/stm32/Core/NanoUI/include/NanoUI",
    ROOT / "firmware/rtl8720dn/NanoUI/src",
)
SHARED_INCLUDE = re.compile(r"^\s*#\s*include\s+<NanoUI/product/([^>]+)>", re.M)


class PublicEntryTests(unittest.TestCase):
    def test_new_app_and_port_reference_use_public_entry(self):
        for relative in (
            "libraries/NanoUI/examples/FirstApp/src/apps/hello_app.h",
            "libraries/NanoUI/integrations/cubemx/board_port.h",
        ):
            with self.subTest(path=relative):
                text = (ROOT / relative).read_text(encoding="utf-8")
                self.assertRegex(text, r"#\s*include\s+<NanoUI\.h>")
                includes = re.findall(r'#\s*include\s+[<"]([^>"\n]+)', text)
                framework = [name for name in includes if name.startswith("NanoUI")]
                self.assertEqual(framework, ["NanoUI.h"])

    def test_key_legacy_paths_still_forward_to_shared_core(self):
        for port in PORTS:
            for relative in (
                "core/window_manager.h",
                "framework/widget.h",
                "framework/app_manager.h",
                "rendering/render_object.h",
                "widgets/container.h",
            ):
                with self.subTest(port=str(port), path=relative):
                    text = (port / relative).read_text(encoding="utf-8")
                    self.assertIn(relative, SHARED_INCLUDE.findall(text))

    def test_forwarding_headers_do_not_add_implementations(self):
        for port in PORTS:
            for path in sorted(port.rglob("*.h")):
                text = path.read_text(encoding="utf-8")
                targets = SHARED_INCLUDE.findall(text)
                if not targets:
                    continue  # Product headers/umbrellas are outside this policy.
                with self.subTest(path=str(path)):
                    for target in targets:
                        self.assertTrue((LIBRARY / "NanoUI/product" / target).is_file())
                    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
                    for line in body.splitlines():
                        line = line.strip()
                        if line:
                            self.assertRegex(line, r"^#\s*(?:include\s|pragma\s+once\s*$)")


if __name__ == "__main__":
    unittest.main()
