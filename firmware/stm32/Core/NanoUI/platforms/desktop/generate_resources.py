"""Desktop fonts plus unchanged product media; never overwrite product resources."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[6]
sys.path.insert(0, str(ROOT / 'libraries/NanoUI/tools'))
from nanoui_tools.config import load_config
from nanoui_tools.fonts import FONT_CPP_MARKER, FONT_H_MARKER, generate_font_blocks, scan_config
from nanoui_tools.output import replace_outputs


def main():
    config = load_config(Path(__file__).with_name('nanoui-assets.json'))
    product = Path(__file__).resolve().parents[2]
    # Read only the original media declarations/bytes, not its font subset.
    header = (product / 'include/NanoUI/nanoui_assets.h').read_text(encoding='utf-8')
    cpp = (product / 'src/nanoui_assets.cpp').read_text(encoding='utf-8')
    if FONT_H_MARKER not in header or FONT_CPP_MARKER not in cpp:
        raise ValueError('Unrecognized original product resource format')
    chars, art, sizes = scan_config(config)
    font_h, font_cpp, count, total = generate_font_blocks(config, chars, art, sizes)
    replace_outputs({config.output_h: header.split(FONT_H_MARKER, 1)[0].rstrip() + font_h,
                     config.output_cpp: cpp.split(FONT_CPP_MARKER, 1)[0].rstrip() + font_cpp})
    print(f'Desktop resources: {count} glyphs, {total} bitmap bytes, sizes={sizes}')


if __name__ == '__main__':
    main()
