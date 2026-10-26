"""Product-only generation: coverage, reproducibility and preserved glyphs/media."""

import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "libraries/NanoUI/tools"))
from nanoui_tools.config import load_config
from nanoui_tools.fonts import generate_font_blocks, scan_config, FONT_CPP_MARKER

ASSET = "firmware/rtl8720dn/NanoUI/src/nanoui_assets.cpp"
BASE = "adc03b6"


def glyphs(source):
    pool = source.split("const uint8_t cn_bitmap_pool[] = {", 1)[1].split("};", 1)[0]
    pool = re.sub(r"//[^\n]*", "", pool)
    data = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})\b", pool))
    result = {}
    for u, size, width, fid, offset in re.findall(
        r"\{0x([0-9a-fA-F]+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}", source
    ):
        size, width, offset = int(size), int(width), int(offset)
        result[int(u, 16), size, int(fid)] = (
            width,
            data[offset : offset + ((width + 7) // 8) * size],
        )
    return result


class ProductFontTests(unittest.TestCase):
    def test_network_trial_pool_is_reproducible_and_separate(self):
        config = load_config(ROOT / "firmware/rtl8720dn/NanoUI/nanoui-network-assets.json")
        chars, art, sizes = scan_config(config)
        source = config.output_cpp.read_text(encoding="utf-8")
        records = glyphs(source)
        for size in sizes:
            for char in chars:
                self.assertIn((ord(char), size, 0), records)
        _, generated, _, _ = generate_font_blocks(config, chars, art, sizes)
        self.assertEqual(
            source.split(FONT_CPP_MARKER, 1)[1], generated.split(FONT_CPP_MARKER, 1)[1]
        )
        self.assertNotEqual(config.output_cpp, ROOT / ASSET)

    def test_product_generation_coverage_and_reproducibility(self):
        config = load_config(ROOT / "firmware/rtl8720dn/NanoUI/nanoui-assets.json")
        self.assertNotIn("app_example", [p.name for p in config.app_dirs])
        chars, art, sizes = scan_config(config)
        source = (ROOT / ASSET).read_text(encoding="utf-8")
        records = glyphs(source)
        for size in sizes:
            for fid, text in ((0, chars), (1, art)):
                for char in text:
                    self.assertIn((ord(char), size, fid), records)
        _, generated, _, _ = generate_font_blocks(config, chars, art, sizes)
        self.assertEqual(
            source.split(FONT_CPP_MARKER, 1)[1], generated.split(FONT_CPP_MARKER, 1)[1]
        )

    def test_existing_glyph_shapes_and_media_preserved(self):
        original = subprocess.check_output(["git", "show", f"{BASE}:{ASSET}"], cwd=ROOT).decode(
            "utf-8"
        )
        current = (ROOT / ASSET).read_text(encoding="utf-8")
        self.assertEqual(original.split(FONT_CPP_MARKER)[0], current.split(FONT_CPP_MARKER)[0])
        before, after = glyphs(original), glyphs(current)
        shared = before.keys() & after.keys()
        # 10px entries are intentionally no longer generated for the 8px body
        # profile; still compare every glyph that survives in the new subset.
        self.assertGreater(len(shared), 200)
        for key in shared:
            if key[1] == 8 and key[2] == 0:
                continue  # Explicit native-8 migration; larger/art glyphs stay unchanged.
            self.assertEqual(before[key], after[key], key)

    def test_both_products_use_the_same_native_8px_shapes(self):
        from nanoui_tools.glyphs import BDFParser

        for filename in (
            "firmware/stm32/Core/NanoUI/nanoui-assets.json",
            "firmware/rtl8720dn/NanoUI/nanoui-assets.json",
        ):
            config = load_config(ROOT / filename)
            override = config.bdf_overrides[8]
            parser = BDFParser(str(override.path), override.alignment)
            records = glyphs(config.output_cpp.read_text(encoding="utf-8"))
            native = [(key, record) for key, record in records.items() if key[1:] == (8, 0)]
            self.assertGreater(len(native), 200)
            for (code, _, _), (width, data) in native:
                expected = parser.get_glyph(code, 8)
                self.assertIsNotNone(expected)
                self.assertEqual((data, width), expected)


if __name__ == "__main__":
    unittest.main()
