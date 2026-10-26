"""Generate NanoUI application font resources from a project config."""

import argparse
import json
import sys
from nanoui_tools.config import load_config
from nanoui_tools.fonts import build_fonts


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="nanoui-assets.json")
    parser.add_argument(
        "--check", action="store_true", help="Validate/pack in memory; do not write outputs"
    )
    args = parser.parse_args(argv)
    try:
        report = build_fonts(load_config(args.config), args.check)
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError) as error:
        print(f"NanoUI font generation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
