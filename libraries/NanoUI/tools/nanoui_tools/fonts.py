"""Generate application-owned font blocks; retain existing media blocks."""

import re
from PIL import ImageFont

from .config import DEFAULT_FONTS, LIBRARY_ROOT
from .glyphs import BDFParser, pack_glyph_ttf
from .output import existing_parts, replace_outputs
from .scan import scan_project

FONT_CPP_MARKER = "// === CHINESE FONT DATA"
FONT_H_MARKER = "// === FONT ENGINE STRUCTURES ==="


def scan_config(config):
    return scan_project(
        config.sources,
        config.scan_mode,
        config.extra_text,
        config.sizes,
        (config.output_h, config.output_cpp, config.provider_path),
        config.text_helpers,
    )


def generate_font_blocks(config, chars, art_chars, sizes):
    parsers = {}
    fonts = {}
    bitmap = []
    index = []
    offset = 0
    for size in sizes:
        override = config.bdf_overrides.get(size)
        filename = (
            override.path
            if override is not None
            else config.fonts_dir / DEFAULT_FONTS.get(size, f"{size}.bdf")
        )
        if filename.is_file():
            parsers[size] = BDFParser(
                str(filename), override.alignment if override is not None else config.alignment
            )
    for size in sizes:
        for font_id, target in ((0, chars), (1, art_chars)):
            if not target:
                continue
            bitmap.append(f"    // --- Font Size: {size}px, ID: {font_id} ---")
            for char in sorted(target):
                result = (
                    parsers[size].get_glyph(ord(char), size)
                    if font_id == 0 and size in parsers
                    else None
                )
                if result is None:
                    if font_id == 0 and size in config.bdf_overrides:
                        raise ValueError(
                            f"No glyph for U+{ord(char):04X} at {size}px in configured "
                            f"BDF override {config.bdf_overrides[size].path}; "
                            "TTF fallback is disabled for overridden default-font sizes."
                        )
                    if config.ttf_font is None:
                        raise ValueError(
                            f"No glyph for U+{ord(char):04X} at {size}px/font {font_id}. "
                            "Set ttf_font to a font file or choose a matching BDF/size."
                        )
                    if size not in fonts:
                        fonts[size] = ImageFont.truetype(str(config.ttf_font), size)
                    result = pack_glyph_ttf(char, fonts[size], size)
                data, width = result
                if not data or not 0 < width <= 255 or len(data) != ((width + 7) // 8) * size:
                    raise ValueError(f"Invalid packed glyph: U+{ord(char):04X} at {size}px")
                index.append((ord(char), size, width, font_id, offset))
                bitmap.append(", ".join(f"0x{byte:02x}" for byte in data) + ",")
                offset += len(data)
    if len(index) > 65535:
        raise ValueError("Font index exceeds the existing uint16_t count format")
    cpp = (
        "\n\n// === CHINESE FONT DATA ===\nconst uint8_t cn_bitmap_pool[] = {\n"
        + "\n".join(bitmap)
        + "\n};\n\nconst uint16_t cn_glyph_count = "
        + str(len(index))
        + ";\nconst CN_GlyphIndex cn_glyph_index[] = {\n"
        + "".join(f"    {{0x{u:04x}, {s}, {w}, {fid}, {off}}},\n" for u, s, w, fid, off in index)
        + "};\n"
    )
    header = (
        "\n\n// === FONT ENGINE STRUCTURES ===\nstruct CN_GlyphIndex {\n"
        "    uint32_t unicode;\n    uint8_t size;\n    uint8_t width;\n"
        "    uint8_t font_id;\n    uint32_t offset;\n};\n"
        "extern const uint8_t cn_bitmap_pool[];\n"
        "extern const CN_GlyphIndex cn_glyph_index[];\n"
        "extern const uint16_t cn_glyph_count;\n\n#endif\n"
    )
    return header, cpp, len(index), offset


def build_fonts(config, check_only=False):
    chars, art, sizes = scan_config(config)
    font_h, font_cpp, count, total = generate_font_blocks(config, chars, art, sizes)
    old_h, old_cpp = existing_parts(config)
    prefix_cpp = old_cpp.split(FONT_CPP_MARKER, 1)[0].rstrip()
    if not prefix_cpp:
        prefix_cpp = f'#include "{config.header_include}"'
    prefix_h = old_h.split(FONT_H_MARKER, 1)[0].rstrip()
    prefix_h = re.sub(r"#endif\s*$", "", prefix_h).rstrip()
    if not prefix_h:
        prefix_h = "#ifndef NANOUI_ASSETS_H\n#define NANOUI_ASSETS_H\n\n#include <stdint.h>"
    elif not prefix_h.startswith("#ifndef NANOUI_ASSETS_H"):
        raise ValueError("Existing resource header is not a NanoUI-generated header")
    outputs = {config.output_h: prefix_h + font_h, config.output_cpp: prefix_cpp + font_cpp}
    if config.font_provider:
        template = (LIBRARY_ROOT / "tools/templates/nanoui_font_provider.cpp.in").read_text(
            encoding="utf-8"
        )
        outputs[config.provider_path] = template.replace("@RESOURCE_HEADER@", config.header_include)
    if not check_only:
        replace_outputs(outputs)
    return {
        "characters": len(chars),
        "sizes": sizes,
        "glyphs": count,
        "bitmap_bytes": total,
        "outputs": [str(path) for path in outputs],
        "written": not check_only,
    }
