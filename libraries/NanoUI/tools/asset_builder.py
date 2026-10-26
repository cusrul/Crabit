"""Open the original media editor for a configured application."""

import argparse
import sys
import tkinter as tk
from nanoui_tools.config import load_config
from nanoui_tools.media_gui import AssetBuilderApp


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="nanoui-assets.json")
    args = parser.parse_args(argv)
    try:
        config = load_config(args.config)
    except (OSError, ValueError) as error:
        print(f"NanoUI media configuration failed: {error}", file=sys.stderr)
        return 1
    root = tk.Tk()
    AssetBuilderApp(root, config)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
