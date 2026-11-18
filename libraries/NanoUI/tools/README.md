# Resource tools

Ordinary example compilation needs no Python. Regeneration needs Python 3.10+
and Pillow (`python -m pip install -r requirements.txt`); video import alone
also needs opencv-python. Tkinter is needed only for the graphical tools.

Create `nanoui-assets.json` beside your application project:

```json
{"app_dirs": ["App"], "output_dir": "Assets/generated"}
```

Use the existing resource selector as the single entry point:

```powershell
python /path/to/NanoUI/tools/resource_gui.py --config /path/to/nanoui-assets.json check
python /path/to/NanoUI/tools/resource_gui.py --config /path/to/nanoui-assets.json fonts
python /path/to/NanoUI/tools/resource_gui.py --config /path/to/nanoui-assets.json images
```

`check` scans and packs fonts in memory without writing; `fonts` writes the
generated fonts; `images` opens the original image/animation editor. Only GUI
actions need Tkinter. With no action, the same entry opens the configuration
selector, including a check button. No extra App manifest or UI directory is
required. Keep dynamic text additions in the existing `extra_text` field.

Existing `font_builder.py --config ... [--check]`, `asset_builder.py --config ...`
and platform wrappers remain compatible.

Paths resolve against the configuration, never the working directory. Generated
`nanoui_assets.h`, `nanoui_assets.cpp` and `nanoui_font_provider.cpp` belong to
the consuming application. Compile the two `.cpp` files and add their directory
to include paths. An application with its own font provider sets
`"font_provider": false`; never compile two font providers.

Defaults scan all source string literals in App and framework sources. This is
not a C++ parser: runtime text, translated strings or custom text helpers need
`extra_text`, `sizes` or `text_helpers`. Default BDF heights are 8, 10, 12, 16,
24; missing glyphs/sizes and STYLE_ART need an explicitly configured `ttf_font`.
There is no implicit Windows font dependency or silent replacement font.
Nested generated/build/examples/experimental/`.git`/`__pycache__`/backup directories are
excluded from scanning (backup names contain `backup`, are `bak`, or end in
`.bak`, case-insensitively). An explicitly configured scan root is still scanned,
even if its name is `examples` or `build`; exclusions apply below that root.

A BDF filename/height does not imply coverage of every character. In particular,
the bundled `5x8.bdf` has no Chinese glyphs. The generator packs every scanned
character at every detected size; an 8px framework Toast therefore also requires
8px coverage for Chinese found in an App, even if that App uses 12px text.
`bdf_overrides` can select a different BDF and alignment for individual sizes; its
paths are relative to the configuration, and the file's declared pixel size must
match the mapping key:

```json
{
  "app_dirs": ["App"],
  "output_dir": "Assets/generated",
  "bdf_overrides": {
    "8": {"path": "Fonts/chinese-8px.bdf", "alignment": "center"}
  }
}
```

An override takes precedence over `DEFAULT_FONTS` only at its configured size.
Other sizes continue using the existing default BDFs and `bdf_alignment`. Missing
glyphs in an overridden default-font size are errors even when `ttf_font` is
configured; they are not silently substituted. Explicit `STYLE_ART` text remains
on its separate TTF path.

Without an override, use a `fonts_dir` containing matching BDF inputs, or explicitly
select a font that covers missing characters, for example:

```json
{"app_dirs": ["App"], "output_dir": "Assets/generated", "ttf_font": "Fonts/your-chinese-font.ttf"}
```

That path is an example, not a bundled font: supply your chosen font file with
appropriate usage rights. Paths remain relative to the configuration. Existing
BDF glyphs remain preferred; TTF is used only where BDF coverage is missing.
Without adequate inputs, generation fails rather than silently choosing a font.

Optional fields: `fonts_dir`, `ttf_font`, `bdf_overrides` (pixel-size string to
`path`/`alignment` mapping), `extra_text` (string list), `sizes` (pixel-height list),
`scan_mode` (`all`/`visible`), `bdf_alignment` (`baseline`/`center`),
`font_provider`, `text_helpers` (identifier list). Unknown fields are rejected.
Legacy layouts can specify `output_h`, `output_cpp`, `header_include` separately.
See `nanoui_tools/config.py` for validation. Outputs retain the original NanoUI
font index and 12-byte media header; fonts preserve media, media preserves fonts.

Generation stages files before replacement and keeps the preceding version in
`.bak` files. Python exceptions during replacement restore already replaced
files. This is not a crash-atomic multi-file transaction; keep resources in Git
and regenerate after interrupted power/process execution. Do not run two
generators against the same output concurrently. Unknown resource formats are
rejected instead of silently overwritten.

Keep product and example outputs separate. The BW16 examples configuration now
writes to `build/font-examples-candidate`, not product `src`; this is an isolated
candidate, not an automatically integrated example build. It does not seed the
product media block: example integration must explicitly provide required media
and build inputs. The known repository configurations have an output-separation
test; the tool does not discover arbitrary competing configurations or lock
concurrent writers. Font and media data still share the existing output format.

The bundled BDF files are original font inputs, not merged generated subsets.
See `../assets/fonts/SOURCES.md` for provenance and redistribution limitations.
