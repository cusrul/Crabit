"""Small project selector; font and media implementations remain shared."""

import argparse
import json
import os
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox
from nanoui_tools.config import load_config
from nanoui_tools.fonts import build_fonts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path)
    args = parser.parse_args()
    root = tk.Tk()
    root.title("NanoUI resources")
    selected = tk.StringVar(value=str(args.config.resolve()) if args.config else "")
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
        ("Generate fonts", fonts),
        ("Images / animation", media),
    ]:
        tk.Button(root, text=label, command=command, width=28).pack(padx=20, pady=5)
    root.mainloop()


if __name__ == "__main__":
    main()
