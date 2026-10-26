import ast
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nanoui_tools.config import DEFAULT_FONTS, LIBRARY_ROOT, load_config
from nanoui_tools.fonts import build_fonts, FONT_CPP_MARKER
from nanoui_tools.glyphs import BDFParser
from nanoui_tools.media import pack_bitmap, export_media
from nanoui_tools.output import replace_outputs
from nanoui_tools.scan import scan_project


def write_test_fonts(directory):
    """Synthetic packing fixtures, not fonts for UI or typography acceptance.

    Keep coverage fixed rather than deriving it from the source scanner: new
    characters/sizes must still expose missing fixture coverage.
    """
    directory.mkdir()
    codepoints = [*range(32, 127), *map(ord, "中文测试")]
    for size, filename in ((8, "5x8.bdf"), (12, "wqy12.bdf"), (16, "wqy16.bdf")):
        lines = [
            "STARTFONT 2.1",
            "FONT NanoUI-Test-Only",
            f"SIZE {size} 75 75",
            f"FONTBOUNDINGBOX 5 {size} 0 0",
            f"CHARS {len(codepoints)}",
        ]
        for codepoint in codepoints:
            bitmap = ["00"] * size if codepoint == 32 else ["F8", *(["88"] * (size - 2)), "F8"]
            lines.extend(
                [
                    f"STARTCHAR U{codepoint:04X}",
                    f"ENCODING {codepoint}",
                    "SWIDTH 500 0",
                    "DWIDTH 5 0",
                    f"BBX 5 {size} 0 0",
                    "BITMAP",
                    *bitmap,
                    "ENDCHAR",
                ]
            )
        (directory / filename).write_text("\n".join([*lines, "ENDFONT", ""]), encoding="ascii")


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        write_test_fonts(self.root / "TestFonts")
        (self.root / "App").mkdir()
        (self.root / "App/main.h").write_text('const char* title = "中文测试";\n', encoding="utf-8")
        self.path = self.root / "nanoui-assets.json"
        self.write_config()

    def write_config(self, **extra):
        data = {"app_dirs": ["App"], "output_dir": "generated", "fonts_dir": "TestFonts"}
        data.update(extra)
        self.path.write_text(json.dumps(data), encoding="utf-8")
        return load_config(self.path)

    def test_relative_config_without_cwd_dependency(self):
        config = load_config(self.path)
        self.assertEqual(config.app_dirs, [self.root / "App"])
        self.assertEqual(config.fonts_dir, self.root / "TestFonts")
        self.assertEqual(config.output_h, self.root / "generated/nanoui_assets.h")
        result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).resolve().parents[1] / "font_builder.py"),
                "--config",
                str(self.path),
                "--check",
            ],
            cwd=self.root / "App",
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(config.output_h.exists())

    def test_chinese_at_all_scanned_sizes_without_system_font(self):
        config = load_config(self.path)
        self.assertIsNone(config.ttf_font)
        self.assertIn("中文测试", (self.root / "App/main.h").read_text(encoding="utf-8"))
        with patch(
            "nanoui_tools.fonts.ImageFont.truetype",
            side_effect=AssertionError("BDF-only fixture must not load a system font"),
        ) as ttf:
            report = build_fonts(config)
        ttf.assert_not_called()
        self.assertEqual(report["sizes"], [8, 12, 16])
        cpp = config.output_cpp.read_text(encoding="utf-8")
        for char in "中文测试":
            for size in report["sizes"]:
                self.assertIn(f"{{0x{ord(char):04x}, {size}, 5, 0, ", cpp)

    def test_default_font_selection_and_alignment_are_unchanged(self):
        config = load_config(self.path)
        self.assertEqual(config.bdf_overrides, {})
        self.assertEqual(
            DEFAULT_FONTS,
            {8: "5x8.bdf", 10: "6x10.bdf", 12: "wqy12.bdf", 16: "wqy16.bdf", 24: "gb24st.bdf"},
        )
        with patch("nanoui_tools.fonts.BDFParser", wraps=BDFParser) as parser:
            build_fonts(config)
        self.assertEqual(
            [(Path(call.args[0]).name, call.args[1]) for call in parser.call_args_list],
            [("5x8.bdf", "baseline"), ("wqy12.bdf", "baseline"), ("wqy16.bdf", "baseline")],
        )

    def test_bdf_override_validation(self):
        valid = {"8": {"path": "TestFonts/5x8.bdf", "alignment": "center"}}
        config = self.write_config(bdf_overrides=valid)
        self.assertEqual(config.bdf_overrides[8].path, self.root / "TestFonts/5x8.bdf")
        self.assertEqual(config.bdf_overrides[8].alignment, "center")
        for invalid in (
            [],
            {"7": {"path": "TestFonts/5x8.bdf", "alignment": "center"}},
            {"65": {"path": "TestFonts/5x8.bdf", "alignment": "center"}},
            {"08": {"path": "TestFonts/5x8.bdf", "alignment": "center"}},
            {"8": {"path": "TestFonts/5x8.bdf", "alignment": "top"}},
            {"8": {"path": "missing.bdf", "alignment": "center"}},
            {"8": {"path": "TestFonts/5x8.bdf", "alignment": "center", "fallback": True}},
            {"8": {"path": "TestFonts/5x8.bdf"}},
            {"8": {"path": "TestFonts/5x8.ttf", "alignment": "center"}},
            {"12": {"path": "TestFonts/5x8.bdf", "alignment": "center"}},
        ):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.write_config(bdf_overrides=invalid)

    def test_bdf_override_generates_native_8px_glyph(self):
        override = self.root / "native8.bdf"
        override.write_text(
            (self.root / "TestFonts/5x8.bdf")
            .read_text(encoding="ascii")
            .replace("DWIDTH 5 0", "DWIDTH 6 0")
            .replace("BBX 5 8 0 0", "BBX 6 8 0 0"),
            encoding="ascii",
        )
        fallback = self.root / "fallback.ttf"
        fallback.write_bytes(b"test-only placeholder")
        config = self.write_config(
            ttf_font=str(fallback),
            bdf_overrides={"8": {"path": "native8.bdf", "alignment": "center"}},
        )
        with patch(
            "nanoui_tools.fonts.ImageFont.truetype",
            side_effect=AssertionError("covered native 8px BDF glyph must not use TTF"),
        ) as ttf:
            build_fonts(config)
        ttf.assert_not_called()
        cpp = config.output_cpp.read_text(encoding="utf-8")
        self.assertIn("{0x4e2d, 8, 6, 0, ", cpp)

    def test_missing_override_glyph_never_falls_back_to_ttf(self):
        fallback = self.root / "fallback.ttf"
        fallback.write_bytes(b"test-only placeholder")
        config = self.write_config(
            ttf_font=str(fallback),
            extra_text=["缺"],
            bdf_overrides={"8": {"path": "TestFonts/5x8.bdf", "alignment": "center"}},
        )
        with patch("nanoui_tools.fonts.ImageFont.truetype") as ttf:
            with self.assertRaisesRegex(ValueError, "TTF fallback is disabled"):
                build_fonts(config)
        ttf.assert_not_called()

    def test_bundled_bdf_missing_chinese_8px_fails_without_creating_outputs(self):
        config = self.write_config(fonts_dir=str(LIBRARY_ROOT / "assets/fonts/bdf"))
        with patch("nanoui_tools.fonts.ImageFont.truetype") as ttf:
            with self.assertRaisesRegex(ValueError, "No glyph for U\\+4E2D at 8px/font 0"):
                build_fonts(config)
        ttf.assert_not_called()
        self.assertFalse(config.output_cpp.parent.exists())

    def test_validation(self):
        for invalid in (
            {"unknown": 1},
            {"app_dirs": []},
            {"header_include": ""},
            {"sizes": [True]},
            {"scan_mode": "anything"},
            {"font_provider": 1},
        ):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.write_config(**invalid)

    def test_scanner_does_not_cross_calls_and_ignores_comments(self):
        (self.root / "App/main.h").write_text(
            '// new Text("忽略", 40);\nnew Text("中文", 12);\n'
            "drawText(x, y, text); unrelated(a, 37);\n"
            'drawText(x, y, "测试", 24);\n',
            encoding="utf-8",
        )
        chars, art, sizes = scan_project([self.root / "App"])
        self.assertNotIn("忽", chars)
        self.assertIn("中", chars)
        self.assertEqual(sizes, [12, 16, 24])

    def test_export_under_build_directory_is_still_scanned(self):
        app = self.root / "build/export/App"
        app.mkdir(parents=True)
        (app / "main.h").write_text('new Text("中文", 12);', encoding="utf-8")
        chars, _, _ = scan_project([app])
        self.assertIn("中", chars)

    def test_cooperation_and_determinism(self):
        config = load_config(self.path)
        report = build_fonts(config)
        self.assertGreater(report["glyphs"], 0)
        initial_font = config.output_cpp.read_text().split(FONT_CPP_MARKER)[1]
        export_media(config, "first", 2, 9, 10, [bytes([1, 128, 1, 0])])
        export_media(config, "second", 1, 1, 20, [b"\x01", b"\x00"])
        self.assertEqual(config.output_cpp.read_text().split(FONT_CPP_MARKER)[1], initial_font)
        build_fonts(config)
        self.assertIn("asset_first[]", config.output_cpp.read_text())
        self.assertIn("asset_second[]", config.output_cpp.read_text())
        previous = config.output_cpp.read_bytes()
        build_fonts(config)
        self.assertEqual(previous, config.output_cpp.read_bytes())
        self.assertEqual(config.output_cpp.with_suffix(".cpp.bak").read_bytes(), previous)
        export_media(config, "first", 1, 1, 10, [b"\x00"])
        self.assertEqual(config.output_cpp.read_text().count("const uint8_t asset_first[]"), 1)
        self.assertIn("asset_second[]", config.output_cpp.read_text())

    def test_missing_glyph_leaves_outputs_untouched(self):
        config = load_config(self.path)
        build_fonts(config)
        build_fonts(config)  # Existing outputs and their last-good backups.
        output_dir = config.output_cpp.parent
        before = {path.name: path.read_bytes() for path in output_dir.iterdir()}
        for path in (config.output_h, config.output_cpp, config.provider_path):
            self.assertIn(path.name, before)
            self.assertIn(path.name + ".bak", before)
        self.write_config(extra_text=["\U0010ffff"])
        with patch("nanoui_tools.fonts.ImageFont.truetype") as ttf:
            with self.assertRaisesRegex(ValueError, "No glyph for U\\+10FFFF"):
                build_fonts(load_config(self.path))
        ttf.assert_not_called()
        self.assertEqual({path.name: path.read_bytes() for path in output_dir.iterdir()}, before)

    def test_replacement_exception_rolls_back(self):
        a, b = self.root / "a.h", self.root / "b.cpp"
        a.write_text("old a")
        b.write_text("old b")
        import nanoui_tools.output as output

        real = output.os.replace

        def fail(source, destination):
            if Path(destination) == b:
                raise OSError("injected replacement failure")
            return real(source, destination)

        with patch.object(output.os, "replace", side_effect=fail):
            with self.assertRaises(OSError):
                replace_outputs({a: "new a", b: "new b"})
        self.assertEqual(a.read_text(), "old a")
        self.assertEqual(b.read_text(), "old b")
        self.assertFalse(list(self.root.glob(".nanoui-*")))

    def test_media_packing_and_header(self):
        image = Image.new("1", (2, 9))
        for position in [(0, 0), (1, 7), (0, 8)]:
            image.putpixel(position, 1)
        self.assertEqual(pack_bitmap(image), b"\x01\x80\x01\x00")
        config = load_config(self.path)
        size = export_media(config, "image", 2, 9, 10, [pack_bitmap(image)])
        self.assertEqual(size, 16)
        import re

        data = bytes(
            int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", config.output_cpp.read_text())
        )
        self.assertEqual(struct.unpack("<HBBHHHH", data[:12]), (0x4E55, 1, 1, 2, 9, 1, 10))
        with self.assertRaises(ValueError):
            export_media(config, "../bad", 1, 1, 1, [b"\x00"])
        with self.assertRaises(ValueError):
            export_media(config, "too_fast", 1, 1, 1001, [b"\x00", b"\x01"])

    def test_unknown_cpp_data_is_not_discarded(self):
        config = load_config(self.path)
        build_fonts(config)
        config.output_cpp.write_text(
            "const int private_data = 42;\n" + config.output_cpp.read_text()
        )
        before = config.output_cpp.read_bytes()
        with self.assertRaisesRegex(ValueError, "Unrecognized resource code"):
            export_media(config, "image", 1, 1, 10, [b"\x01"])
        self.assertEqual(config.output_cpp.read_bytes(), before)

    def test_original_bdf_bytes_on_both_alignment_profiles(self):
        repo = Path(__file__).resolve().parents[4]
        if not (repo / ".git").exists():
            self.skipTest("Historical byte comparison requires the Crabit Git baseline")
        for path, alignment in [
            ("firmware/stm32/Core/NanoUI/tools/font_builder.py", "baseline"),
            ("firmware/rtl8720dn/NanoUI/tools/font_builder.py", "center"),
        ]:
            source = subprocess.check_output(["git", "show", "9525ab9:" + path], cwd=repo).decode(
                "utf-8-sig"
            )
            node = next(
                item
                for item in ast.parse(source).body
                if isinstance(item, ast.ClassDef) and item.name == "BDFParser"
            )
            import os

            scope = {"os": os, "Image": Image}
            exec(compile(ast.Module(body=[node], type_ignores=[]), path, "exec"), scope)
            # Historical byte identity must keep using the real bundled input,
            # not the synthetic fonts used by normal-flow tool tests.
            font = LIBRARY_ROOT / "assets/fonts/bdf/wqy12.bdf"
            old = scope["BDFParser"](str(font))
            new = BDFParser(str(font), alignment)
            for char in "中文ABgj012":
                self.assertEqual(
                    new.get_glyph(ord(char), 12), old.get_glyph(ord(char), 12), (alignment, char)
                )


if __name__ == "__main__":
    unittest.main()
