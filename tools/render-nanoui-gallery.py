"""Present actual C++ raster outputs without recreating the UI in Python."""

from pathlib import Path
import argparse
import hashlib
import json
from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument(
        "--verify", type=Path, help="Check recorded pixels; never rewrite the baseline"
    )
    args = parser.parse_args()
    pages = sorted(args.directory.glob("*.ppm"))
    if not pages:
        raise SystemExit("No pipeline-rendered PPM files found")
    if args.verify:
        expected = {
            item["name"]: item["sha256"]
            for item in json.loads(args.verify.read_text(encoding="utf-8"))
        }
        actual = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in pages}
        if actual != expected:
            raise SystemExit("Rendered pixels differ from the recorded visual checkpoint")
        print(f"Visual checkpoint matches: {len(pages)} pages")
    tiles = []
    for path in pages:
        with Image.open(path) as source:
            image = source.convert("RGB")
        image.save(path.with_suffix(".png"))
        tile = Image.new("RGB", (404, 328), "#303030")
        ImageDraw.Draw(tile).text((10, 8), path.stem, fill="white")
        tile.paste(
            image.resize((image.width * 3, image.height * 3), Image.Resampling.NEAREST), (10, 30)
        )
        tiles.append(tile)
    sheet = Image.new("RGB", (404 * 4, 328 * ((len(tiles) + 3) // 4)), "#303030")
    for index, tile in enumerate(tiles):
        sheet.paste(tile, ((index % 4) * 404, (index // 4) * 328))
    output = args.directory / "contact-sheet.png"
    sheet.save(output)
    print(output.resolve())


if __name__ == "__main__":
    main()
