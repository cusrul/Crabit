"""Compatibility entry: resource implementation lives in libraries/NanoUI."""
from pathlib import Path
import runpy
import sys

if __name__ == "__main__":
    config = Path(__file__).resolve().parent.parent / "nanoui-assets.json"
    entry = Path(__file__).resolve().parents[4] / "libraries/NanoUI/tools/asset_builder.py"
    sys.path.insert(0, str(entry.parent))
    sys.argv = [str(entry), "--config", str(config), *sys.argv[1:]]
    runpy.run_path(str(entry), run_name="__main__")
