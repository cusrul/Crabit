"""Original BDF/TTF packing algorithms; target behavior is an explicit setting."""

from PIL import Image, ImageDraw
import os


class BDFParser:
    def __init__(self, filename, alignment="baseline"):
        self.filename = filename
        self.alignment = alignment
        self.glyphs = {}
        if os.path.exists(filename):
            self._parse()

    def _parse(self):
        try:
            with open(self.filename, "r", encoding="ascii", errors="ignore") as f:
                current_glyph = None
                in_bitmap = False
                for line in f:
                    line = line.strip()
                    if line.startswith("STARTCHAR"):
                        current_glyph = {}
                    elif line.startswith("ENCODING"):
                        current_glyph["unicode"] = int(line.split()[1])
                    elif line.startswith("DWIDTH"):
                        current_glyph["advance"] = int(line.split()[1])
                    elif line.startswith("BBX"):
                        parts = line.split()
                        current_glyph["w"] = int(parts[1])
                        current_glyph["h"] = int(parts[2])
                        current_glyph["x_off"] = int(parts[3])
                        current_glyph["y_off"] = int(parts[4])
                    elif line.startswith("BITMAP"):
                        in_bitmap = True
                        current_glyph["bitmap"] = []
                    elif line.startswith("ENDCHAR"):
                        in_bitmap = False
                        if current_glyph and "unicode" in current_glyph:
                            self.glyphs[current_glyph["unicode"]] = current_glyph
                    elif in_bitmap:
                        current_glyph["bitmap"].append(line)
        except:
            pass

    def get_glyph(self, char_code, target_height):
        glyph = self.glyphs.get(char_code)
        if not glyph:
            return None
        width, height = glyph["w"], glyph["h"]
        advance = glyph.get("advance", width)
        img = Image.new("1", (advance, target_height), 0)
        pixels = img.load()
        # 还原原始对齐算法
        start_y = (
            (target_height - height) // 2
            if self.alignment == "center"
            else target_height - height - (glyph["y_off"] + 2)
        )
        if start_y < 0:
            start_y = 0

        for row_idx, hex_str in enumerate(glyph["bitmap"]):
            y = start_y + row_idx
            if y >= target_height:
                break
            try:
                bits = bin(int(hex_str, 16))[2:].zfill(len(hex_str) * 4)
                for x_idx, bit in enumerate(bits):
                    x = glyph["x_off"] + x_idx
                    if 0 <= x < advance and bit == "1":
                        pixels[x, y] = 1
            except:
                pass

        packed_data = []
        byte_width = (advance + 7) // 8
        for y in range(target_height):
            for b in range(byte_width):
                byte = 0
                for bit in range(8):
                    if b * 8 + bit < advance and pixels[b * 8 + bit, y]:
                        byte |= 128 >> bit
                packed_data.append(byte)
        return bytes(packed_data), advance


def pack_glyph_ttf(char, font, size):
    width = int(font.getlength(char))
    if width <= 0:
        bbox = font.getbbox(char)
        width = bbox[2] - bbox[0]
    if width <= 0:
        width = size // 2
    img = Image.new("1", (width, size), 0)
    draw = ImageDraw.Draw(img)
    draw.text((0, 0), char, font=font, fill=1)
    pixels = img.load()
    packed_data = []
    byte_width = (width + 7) // 8
    for y in range(size):
        for b in range(byte_width):
            byte = 0
            for bit in range(8):
                if b * 8 + bit < width and pixels[b * 8 + bit, y]:
                    byte |= 128 >> bit
            packed_data.append(byte)
    return bytes(packed_data), width
