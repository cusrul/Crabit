"""Compare real NanoUI BMP snapshots against a separately built baseline."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

SCENES = [
    "launcher",
    "sensors",
    "system",
    "games",
    "tools",
    "fonts",
    "nested",
    "clock",
    "launcher-down-5",
    "system-down-4",
    "sensors-down-4",
    "tools-down-5",
    "nested-down-2",
    "navigate-sensors",
    "navigate-back",
]
EXPECTED_DELTAS = {"sensors", "games", "fonts", "navigate-sensors"}


def pixels(path):
    data = path.read_bytes()
    assert data[:2] == b"BM" and struct.unpack_from("<H", data, 28)[0] == 24, path
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    assert (width, height) == (512, 256), path
    # The observation harness exports nearest-neighbour 4x physical pixels.
    stride = (width * 3 + 3) & ~3
    logical = [
        data[offset + (height - 1 - y * 4) * stride + x * 12] for y in range(64) for x in range(128)
    ]
    return logical, hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    results = []
    changed = set()
    for scene in SCENES:
        before, before_hash = pixels(args.directory / "baseline" / (scene + ".bmp"))
        after, after_hash = pixels(args.directory / (scene + ".bmp"))
        indices = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
        if indices:
            changed.add(scene)
        results.append(
            {
                "scene": scene,
                "different_logical_pixels": len(indices),
                "before_sha256": before_hash,
                "after_sha256": after_hash,
            }
        )
    print(json.dumps(results, indent=2))
    if changed != EXPECTED_DELTAS:
        raise SystemExit(
            f"Unexpected changed scenes: {sorted(changed)}; expected {sorted(EXPECTED_DELTAS)}"
        )
    print(
        f"PASS: {len(SCENES) - len(changed)} identical scenes, {len(changed)} explicitly reviewed layout deltas"
    )


if __name__ == "__main__":
    main()
