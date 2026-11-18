"""Single NanoUI resource entry: headless checks/generation and the image editor."""

import argparse
import json
import os
from pathlib import Path
import sys

from nanoui_tools.config import load_config
from nanoui_tools.fonts import build_fonts

ACTIONS = ("gui", "check", "fonts", "images")


def launch_gui(action, *args):
    try:
        import tkinter as tk
    except ImportError as error:
        print(f"NanoUI GUI needs Tkinter: {error}", file=sys.stderr)
        return 1
    try:
        return action(*args)
    except tk.TclError as error:
        print(f"NanoUI GUI cannot open a display: {error}", file=sys.stderr)
        return 1


def report_fonts(config, check_only):
    report = build_fonts(config, check_only)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


def open_images(config):
    import tkinter as tk
    from nanoui_tools.media_gui import AssetBuilderApp

    root = tk.Tk()
    root.title("NanoUI images / animation")
    AssetBuilderApp(root, config)
    root.mainloop()
    return 0


def open_selector(initial):
    import tkinter as tk
    from tkinter import filedialog, messagebox

    root = tk.Tk()
    root.title("NanoUI resources")
    selected = tk.StringVar(value=initial)
    tk.Label(root, text="Project configuration (paths are relative to this file)").pack(
        padx=20, pady=10
    )
    tk.Entry(root, textvariable=selected, width=75).pack(padx=20)

    def open_config():
        value = filedialog.askopenfilename(filetypes=[("NanoUI configuration", "*.json")])
        if value:
            selected.set(value)

    def create_config():
        app = filedialog.askdirectory(title="Application source directory")
        if not app:
            return
        output = filedialog.askdirectory(title="Generated resource directory")
        if not output:
            return
        filename = filedialog.asksaveasfilename(
            initialfile="nanoui-assets.json", defaultextension=".json"
        )
        if not filename:
            return
        path = Path(filename).resolve()

        def relative(value):
            try:
                return Path(os.path.relpath(value, path.parent)).as_posix()
            except ValueError:  # Different Windows drives.
                return Path(value).as_posix()

        data = {"app_dirs": [relative(app)], "output_dir": relative(output)}
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        selected.set(str(path))

    def check():
        try:
            report = build_fonts(load_config(selected.get()), True)
            messagebox.showinfo(
                "Resource check",
                f"{report['characters']} characters, {report['glyphs']} glyphs, "
                f"sizes {report['sizes']}; nothing written",
            )
        except Exception as error:
            messagebox.showerror("Resource check failed", str(error))

    def fonts():
        try:
            report = build_fonts(load_config(selected.get()))
            messagebox.showinfo(
                "Font resources",
                f"{report['glyphs']} glyphs; {report['bitmap_bytes']} bitmap bytes",
            )
        except Exception as error:
            messagebox.showerror("Generation failed", str(error))

    def media():
        try:
            config = load_config(selected.get())
            from nanoui_tools.media_gui import AssetBuilderApp

            AssetBuilderApp(tk.Toplevel(root), config)
        except Exception as error:
            messagebox.showerror("Cannot open media tool", str(error))

    for label, command in [
        ("Open configuration", open_config),
        ("New configuration", create_config),
        ("Check resources", check),
        ("Generate fonts", fonts),
        ("Images / animation", media),
    ]:
        tk.Button(root, text=label, command=command, width=28).pack(padx=20, pady=5)
    root.mainloop()
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action",
        nargs="?",
        choices=ACTIONS,
        help="check: scan/pack in memory; fonts: write fonts; "
        "images: open the image editor; gui (default): project selector",
    )
    parser.add_argument("--config", type=Path)
    args = parser.parse_args(argv)
    if args.action in (None, "gui"):
        return launch_gui(open_selector, str(args.config.resolve()) if args.config else "")
    if args.config is None:
        parser.error(f"--config is required for the {args.action} action")
    try:
        config = load_config(args.config)
        if args.action == "check":
            return report_fonts(config, True)
        if args.action == "fonts":
            return report_fonts(config, False)
    except (OSError, ValueError) as error:
        print(f"NanoUI resources failed: {error}", file=sys.stderr)
        return 1
    return launch_gui(open_images, config)


if __name__ == "__main__":
    raise SystemExit(main())
