"""Checks the offline paint extraction, never SDK/radio behavior."""

import unittest

from tools import render_bw16_small_fonts as preview


class SmallFontPreviewTests(unittest.TestCase):
    def test_extracts_product_paint_block_not_input_handler(self):
        source = preview.baseline_source()
        paint = source[source.index("uint32_t ui_throttle_ms") :]
        scan = preview.block(paint, "if (state == STATE_MODE_SEL)")
        self.assertIn('canvas->drawText(2, 2, "选择扫描方式", 8, true)', scan)
        self.assertIn('canvas->drawText(4, 55, "确认键开始", 8, false)', scan)
        for forbidden in ("wifi_", "safe_delay", "input->", "EVT_OK"):
            self.assertNotIn(forbidden, scan)

    def test_known_bitmap_sources_cover_chinese_only_at_twelve(self):
        samples = set("选择扫描方式被动监听系统确认键开始任务")
        for size, name, expected in (
            (8, "5x8.bdf", 0),
            (10, "6x10.bdf", 0),
            (12, "wqy12.bdf", len(samples)),
        ):
            with self.subTest(size=size):
                font = preview.BDFParser(str(preview.BW / "assets/fonts/bdf" / name))
                self.assertEqual(sum(ord(c) in font.glyphs for c in samples), expected)

    def test_original_panel_is_substituted_without_rewriting_it(self):
        sample = "if (state == STATE_MODE_SEL) { canvas->clear(); }"
        rendered = preview.DRIVER.replace("@SCAN_BLOCK@", sample)
        self.assertIn(sample, rendered)
        self.assertNotIn("@SCAN_BLOCK@", rendered)


if __name__ == "__main__":
    unittest.main()
