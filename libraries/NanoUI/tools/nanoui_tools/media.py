"""Existing 12-byte NanoUI media header and vertical-page bitmap format."""

import re
import struct
from .fonts import FONT_CPP_MARKER, FONT_H_MARKER
from .output import existing_parts, replace_outputs


def pack_bitmap(image):
    image = image.convert("1")
    width, height = image.size
    pixels = image.load()
    packed = bytearray()
    for page in range((height + 7) // 8):
        for x in range(width):
            byte = 0
            for bit in range(8):
                y = page * 8 + bit
                if y < height and pixels[x, y]:
                    byte |= 1 << bit
            packed.append(byte)
    return bytes(packed)


def export_media(config, name, width, height, fps, frames):
    if not re.fullmatch(r"[A-Za-z_]\w*", name, flags=re.ASCII):
        raise ValueError("Asset name must be a C identifier, e.g. my_image")
    if not all(type(v) is int and 0 < v <= 65535 for v in (width, height, fps)):
        raise ValueError("Width, height and FPS must be integers from 1 to 65535")
    if not 0 < len(frames) <= 65535:
        raise ValueError("Frame count must be from 1 to 65535")
    if len(frames) > 1 and fps > 1000:
        raise ValueError(
            "Animation FPS must not exceed the runtime's millisecond resolution (1000)"
        )
    expected = width * ((height + 7) // 8)
    if any(len(frame) != expected for frame in frames):
        raise ValueError("Packed frame length does not match the media dimensions")
    header = struct.pack(
        "<HBBHHHH", 0x4E55, 1, 2 if len(frames) > 1 else 1, width, height, len(frames), fps
    )
    old_h, old_cpp = existing_parts(config)
    font_cpp = old_cpp[old_cpp.index(FONT_CPP_MARKER) :] if FONT_CPP_MARKER in old_cpp else ""
    font_h = old_h[old_h.index(FONT_H_MARKER) :] if FONT_H_MARKER in old_h else ""
    prefix_cpp = old_cpp.split(FONT_CPP_MARKER, 1)[0]
    # Replace only this named asset; retain every other marked media block.
    blocks = re.findall(
        r"(// --- Asset: [^\n]+\nconst uint8_t asset_(\w+)\[\] = \{.*?\n\};)",
        prefix_cpp,
        flags=re.S,
    )
    remaining = prefix_cpp
    for block, _ in blocks:
        remaining = remaining.replace(block, "", 1)
    remaining = re.sub(r"^\s*#include\s+[^\n]+", "", remaining, flags=re.M)
    remaining = re.sub(r"//[^\n]*|/\*.*?\*/", "", remaining, flags=re.S)
    if remaining.strip():
        raise ValueError("Unrecognized resource code; refusing to discard application data")
    others = [block for block, identifier in blocks if identifier != name]
    names = [identifier for _, identifier in blocks if identifier != name]
    declarations = re.findall(r"extern const uint8_t asset_(\w+)\[\];", old_h)
    if set(declarations) - {name} != set(names):
        raise ValueError("Unrecognized existing asset blocks; refusing a destructive export")
    cpp = f'#include "{config.header_include}"\n\n'
    cpp += "\n\n".join(others) + ("\n\n" if others else "")
    cpp += (
        f"// --- Asset: {name} ({width}x{height}, {len(frames)} frames) ---\n"
        f"const uint8_t asset_{name}[] = {{\n"
    )
    cpp += "    " + ", ".join(f"0x{b:02X}" for b in header) + ", // Header\n"
    for frame in frames:
        for start in range(0, len(frame), 16):
            cpp += "    " + ", ".join(f"0x{b:02X}" for b in frame[start : start + 16]) + ",\n"
    cpp += "};\n\n" + font_cpp
    h = "#ifndef NANOUI_ASSETS_H\n#define NANOUI_ASSETS_H\n\n#include <stdint.h>\n\n"
    h += "".join(f"extern const uint8_t asset_{item}[];\n" for item in names + [name])
    h += "\n" + (font_h if font_h else "#endif\n")
    replace_outputs({config.output_h: h, config.output_cpp: cpp})
    return len(header) + sum(map(len, frames))
