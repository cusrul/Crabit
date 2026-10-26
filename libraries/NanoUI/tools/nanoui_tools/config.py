"""Resolve all user paths against the project config, never the process cwd."""

from dataclasses import dataclass
import json
from pathlib import Path

LIBRARY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FONTS = {8: "5x8.bdf", 10: "6x10.bdf", 12: "wqy12.bdf", 16: "wqy16.bdf", 24: "gb24st.bdf"}


@dataclass
class BDFOverride:
    path: Path
    alignment: str


@dataclass
class ResourceConfig:
    path: Path
    app_dirs: list[Path]
    output_h: Path
    output_cpp: Path
    header_include: str
    fonts_dir: Path
    ttf_font: Path | None
    scan_mode: str
    alignment: str
    bdf_overrides: dict[int, BDFOverride]
    extra_text: list[str]
    sizes: list[int]
    font_provider: bool
    text_helpers: list[str]

    @property
    def sources(self):
        return self.app_dirs + [LIBRARY_ROOT / "src" / "NanoUI" / "product"]

    @property
    def provider_path(self):
        return self.output_cpp.parent / "nanoui_font_provider.cpp"


def load_config(filename):
    path = Path(filename).resolve()
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError) as error:
        raise ValueError(f"Cannot read project config {path}: {error}") from error
    if not isinstance(data, dict):
        raise ValueError("Project config must be a JSON object")
    known = {
        "app_dirs",
        "output_dir",
        "output_h",
        "output_cpp",
        "header_include",
        "fonts_dir",
        "ttf_font",
        "scan_mode",
        "bdf_alignment",
        "bdf_overrides",
        "extra_text",
        "sizes",
        "font_provider",
        "text_helpers",
    }
    unknown = set(data) - known
    if unknown:
        raise ValueError(f"Unknown config fields: {', '.join(sorted(unknown))}")

    def resolve(value):
        if not isinstance(value, str) or not value:
            raise ValueError("Paths must be non-empty strings")
        return (path.parent / value).resolve()

    app_dirs = data.get("app_dirs")
    if isinstance(app_dirs, str):
        app_dirs = [app_dirs]
    if not isinstance(app_dirs, list) or not app_dirs:
        raise ValueError("Set app_dirs to your application source directory/list")
    sources = [resolve(value) for value in app_dirs]
    for source in sources:
        if not source.is_dir():
            raise ValueError(f"Application source directory does not exist: {source}")
    output = resolve(data.get("output_dir", "generated"))
    header = resolve(data["output_h"]) if "output_h" in data else output / "nanoui_assets.h"
    cpp = resolve(data["output_cpp"]) if "output_cpp" in data else output / "nanoui_assets.cpp"
    if header == cpp or header.suffix != ".h" or cpp.suffix != ".cpp":
        raise ValueError("Resource output must be distinct .h and .cpp files")
    include = data.get("header_include", header.name)
    if not isinstance(include, str) or not include or any(c in include for c in '\n\r"'):
        raise ValueError("header_include must be a quoted-include-safe path")
    fonts_dir = (
        resolve(data["fonts_dir"]) if "fonts_dir" in data else LIBRARY_ROOT / "assets/fonts/bdf"
    )
    ttf = resolve(data["ttf_font"]) if data.get("ttf_font") else None
    if ttf is not None and not ttf.is_file():
        raise ValueError(f"Configured TTF font does not exist: {ttf}")
    mode = data.get("scan_mode", "all")
    alignment = data.get("bdf_alignment", "baseline")
    if mode not in ("visible", "all") or alignment not in ("baseline", "center"):
        raise ValueError("scan_mode: visible/all; bdf_alignment: baseline/center")
    raw_overrides = data.get("bdf_overrides", {})
    if not isinstance(raw_overrides, dict):
        raise ValueError("bdf_overrides must map pixel-size strings to override objects")
    bdf_overrides = {}
    for raw_size, raw_override in raw_overrides.items():
        if not isinstance(raw_size, str) or not raw_size.isdecimal():
            raise ValueError(f"Invalid bdf_overrides size: {raw_size!r}")
        size = int(raw_size)
        if str(size) != raw_size or not 8 <= size <= 64:
            raise ValueError(
                f"bdf_overrides size must be a canonical integer from 8 to 64: {raw_size}"
            )
        if not isinstance(raw_override, dict):
            raise ValueError(f"bdf_overrides[{raw_size}] must be an object")
        override_fields = {"path", "alignment"}
        unknown_override_fields = set(raw_override) - override_fields
        missing_override_fields = override_fields - set(raw_override)
        if unknown_override_fields:
            raise ValueError(
                f"Unknown bdf_overrides[{raw_size}] fields: "
                f"{', '.join(sorted(unknown_override_fields))}"
            )
        if missing_override_fields:
            raise ValueError(f"bdf_overrides[{raw_size}] requires path and alignment")
        override_path = resolve(raw_override["path"])
        if override_path.suffix.lower() != ".bdf" or not override_path.is_file():
            raise ValueError(f"BDF override must name an existing .bdf file: {override_path}")
        override_alignment = raw_override["alignment"]
        if override_alignment not in ("baseline", "center"):
            raise ValueError(f"bdf_overrides[{raw_size}].alignment must be baseline or center")
        try:
            with override_path.open("r", encoding="ascii") as font_file:
                if not font_file.readline().strip().startswith("STARTFONT"):
                    raise ValueError("missing STARTFONT header")
                declared_size = None
                for line in font_file:
                    parts = line.split()
                    if parts and parts[0] == "SIZE":
                        declared_size = int(parts[1])
                        break
                    if parts and parts[0] == "CHARS":
                        break
        except (OSError, UnicodeError, ValueError, IndexError) as error:
            raise ValueError(f"Invalid BDF override file {override_path}: {error}") from error
        if declared_size != size:
            raise ValueError(
                f"BDF override {override_path} declares {declared_size}px, not configured {size}px"
            )
        bdf_overrides[size] = BDFOverride(override_path, override_alignment)
    text = data.get("extra_text", [])
    sizes = data.get("sizes", [])
    if not isinstance(text, list) or not all(isinstance(s, str) for s in text):
        raise ValueError("extra_text must be a list of strings")
    if not isinstance(sizes, list) or not all(type(s) is int and 8 <= s <= 64 for s in sizes):
        raise ValueError("sizes must be integer pixel heights from 8 to 64")
    provider = data.get("font_provider", True)
    if type(provider) is not bool:
        raise ValueError("font_provider must be true or false")
    helpers = data.get("text_helpers", [])
    if not isinstance(helpers, list) or not all(
        isinstance(s, str) and s.isidentifier() for s in helpers
    ):
        raise ValueError("text_helpers must be function identifier strings")
    return ResourceConfig(
        path,
        sources,
        header,
        cpp,
        include,
        fonts_dir,
        ttf,
        mode,
        alignment,
        bdf_overrides,
        text,
        sizes,
        provider,
        helpers,
    )
