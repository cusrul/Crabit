"""Ensure showcase guide image links resolve from their Markdown files."""

import re
import unittest
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "showcase" / "docs"
IMAGE_LINK = re.compile(r"!\[[^\]]*\]\(\s*(?:<([^>]+)>|([^\s)]+))")


class ShowcaseImageLinksTests(unittest.TestCase):
    def test_local_image_links_resolve_without_legacy_media_root(self):
        links = []
        errors = []
        for document in sorted(DOCS.glob("*.md")):
            source = document.read_text(encoding="utf-8")
            for match in IMAGE_LINK.finditer(source):
                target = match.group(1) or match.group(2)
                if target.startswith("/media/crabit/"):
                    errors.append(f"{document.relative_to(ROOT)}: legacy root: {target}")
                    continue
                parsed = urlsplit(target)
                if parsed.scheme or parsed.netloc or target.startswith("#"):
                    continue
                links.append((document, target))
                resolved = (document.parent / unquote(parsed.path)).resolve()
                if not resolved.is_file():
                    errors.append(f"{document.relative_to(ROOT)}: missing image: {target}")

        self.assertGreater(len(links), 0, "expected to check local showcase image links")
        self.assertEqual([], errors, "\n".join(errors))


if __name__ == "__main__":
    unittest.main()
