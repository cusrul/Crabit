"""Validate generated desktop review evidence, not embedded/hardware behavior."""

import hashlib
import json
import subprocess
from pathlib import Path
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/watch-ui-review"


class WatchUiReviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (OUT / "manifest.tsv").is_file():
            raise unittest.SkipTest(
                "Run tools/build_watch_ui_review.py to generate desktop candidates"
            )
        cls.frames = {}
        for line in (OUT / "manifest.tsv").read_text(encoding="utf-8").splitlines():
            fields = line.split("\t")
            cls.frames[fields[0]] = fields[1].replace("{frame}", "000")

    def test_native_font_coverage_has_no_silent_fallback(self):
        report = json.loads((OUT / "font-coverage.json").read_text(encoding="utf-8"))
        self.assertEqual(report["sizes"], [8, 10, 12])
        self.assertEqual(report["missing_glyphs"], [])
        self.assertFalse(report["small_font_ttf_fallback"])
        for size, expected in report["font_archive_sha256"].items():
            archive = (
                ROOT
                / f"build/ui-review-fonts/fusion-pixel-font-{size}px-monospaced-bdf-v{report['font_release']}.zip"
            )
            self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), expected)

    def test_windows_viewer_loads_manifest_images(self):
        viewer = OUT / "watch-ui-motion-review.exe"
        if not viewer.is_file():
            self.skipTest("Windows viewer not built")
        subprocess.run([str(viewer), "--check-images"], check=True, timeout=30)

    def test_all_selectable_frames_are_native_binary_pixels(self):
        self.assertEqual(len(self.frames), 34)
        for label, filename in self.frames.items():
            with self.subTest(label=label), Image.open(filename) as frame:
                self.assertEqual(frame.size, (128, 64))
                self.assertEqual(set(frame.convert("L").getdata()), {0, 255})

    def test_candidates_and_font_sizes_are_visibly_distinct(self):
        for size in (8, 10, 12):
            self.assertNotEqual(
                (OUT / f"face-0-{size}.pgm").read_bytes(), (OUT / f"face-2-{size}.pgm").read_bytes()
            )
            self.assertNotEqual(
                (OUT / f"weather-0-{size}.pgm").read_bytes(),
                (OUT / f"weather-1-{size}.pgm").read_bytes(),
            )
        self.assertNotEqual(
            (OUT / "settings-8-top.pgm").read_bytes(), (OUT / "settings-10-top.pgm").read_bytes()
        )

    def test_motion_frames_have_readable_holds_and_real_transitions(self):
        for variant in (0, 1):
            frames = []
            for frame in range(80):
                with Image.open(OUT / f"motion-{variant}-{frame:03d}.bmp") as image:
                    pixels = image.convert("L")
                    self.assertEqual(pixels.size, (128, 64))
                    self.assertEqual(set(pixels.getdata()), {0, 255})
                    frames.append(pixels.copy())
            self.assertEqual(frames[0].tobytes(), frames[19].tobytes())
            self.assertEqual(frames[39].tobytes(), frames[59].tobytes())
            self.assertNotEqual(frames[0].tobytes(), frames[39].tobytes())
            self.assertGreater(len({frame.tobytes() for frame in frames}), 8)
            for image in frames:
                left, top, right, bottom = image.getbbox()
                self.assertGreaterEqual(left, 19)
                self.assertLessEqual(right, 109)
                self.assertGreaterEqual(top, 14)
                self.assertLessEqual(bottom, 50)
            if variant == 1:
                # Clipping must not expose the incoming/outgoing digit at rest.
                for index in (0, 39):
                    _, top, _, bottom = frames[index].getbbox()
                    self.assertLessEqual(bottom - top, 21)

    def test_liquid_points_move_off_the_resting_dot_grid(self):
        with Image.open(OUT / "motion-0-000.bmp") as image:
            image = image.convert("L")
            self.assertTrue(
                all(
                    ((x - 19) % 3 < 2 and (y - 14) % 3 < 2)
                    for y in range(64)
                    for x in range(128)
                    if image.getpixel((x, y))
                )
            )
        with Image.open(OUT / "motion-0-030.bmp") as image:
            image = image.convert("L")
            self.assertTrue(
                any(
                    ((x - 19) % 3 == 2 or (y - 14) % 3 == 2)
                    for y in range(64)
                    for x in range(128)
                    if image.getpixel((x, y))
                )
            )


if __name__ == "__main__":
    unittest.main()
