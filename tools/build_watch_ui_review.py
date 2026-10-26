"""Windows-only real NanoUI design review; never changes product/generated firmware assets."""

from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/watch-ui-review"
CACHE = ROOT / "build/ui-review-fonts"
SOURCES = ROOT / "tools/ui_review"
PRODUCT = ROOT / "firmware/stm32/Core/NanoUI/app_real"
FONT_TAG = "2026.10.16"
FONT_HASHES = {
    8: "a090b06a7275a9f392c9f19d780cc9336275e57c79d3b6a4e54a3716032fdf64",
    10: "dd382ff92fa81cd3ae233bdd7e1a8e5122e8ec495fa7e8dbc7c3d6e8b3410a65",
    12: "76ed4adde0cc43ae8312041960d586c130c75a9d0f7c1c64a7cace7e4130c358",
}
sys.path.insert(0, str(ROOT / "libraries/NanoUI/tools"))
from nanoui_tools.glyphs import BDFParser  # noqa: E402


def fonts():
    CACHE.mkdir(parents=True, exist_ok=True)
    result = {}
    for size, expected in FONT_HASHES.items():
        name = f"fusion-pixel-font-{size}px-monospaced-bdf-v{FONT_TAG}.zip"
        archive = CACHE / name
        if not archive.exists():
            urllib.request.urlretrieve(
                f"https://github.com/TakWolf/fusion-pixel-font/releases/download/{FONT_TAG}/{name}",
                archive,
            )
        if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Font archive hash mismatch: {archive}")
        directory = CACHE / str(size)
        wanted = directory / f"fusion-pixel-{size}px-monospaced-zh_hans.bdf"
        if not wanted.exists():
            with zipfile.ZipFile(archive) as package:
                for entry in package.infolist():
                    resolved = (directory / entry.filename).resolve()
                    if not resolved.is_relative_to(directory.resolve()):
                        raise ValueError("Unsafe archive path")
                package.extractall(directory)
        result[size] = BDFParser(str(wanted), "center")
    return result


def stage_settings():
    """Mechanical host-only candidate: real page/controls, translated labels and font parameter."""
    translations = {
        "SETTINGS": "设置",
        "SETTINGS *": "设置 *",
        "Brightness": "亮度",
        "Invert": "反色",
        "Screen off": "息屏",
        "Sleep +": "休眠延时",
        "After screen off": "息屏后开始计时",
        "Bluetooth": "蓝牙",
        "APPLY": "应用",
        "BACK": "返回",
        "OFF": "关闭",
        "ON": "开启",
        "BLE OFF": "蓝牙已关闭",
        "NEVER": "不休眠",
        "CUSTOM": "自定义",
        "APPLIED": "已应用",
        "NO CHANGES": "没有修改",
    }
    source = (PRODUCT / "app_settings.h").read_text(encoding="utf-8")
    for english, chinese in translations.items():
        source = source.replace(f'"{english}"', f'(previewChinese ? "{chinese}" : "{english}")')
    source = source.replace(", 8)", ", previewFontSize)")
    source = source.replace("24, 8)", "24, 12)")
    (OUT / "app_settings_preview.h").write_text(source, encoding="utf-8")
    components = OUT / "components"
    components.mkdir(exist_ok=True)
    action = (PRODUCT / "components/action_button.h").read_text(encoding="utf-8")
    action = action.replace("new Text(text)", "new Text(text, previewFontSize)")
    (components / "action_button.h").write_text(action, encoding="utf-8")
    picker = (PRODUCT / "components/settings_option_picker.h").read_text(encoding="utf-8")
    picker = picker.replace(
        "std::strlen(current) * 6", "get_utf8_string_width(current, previewFontSize)"
    )
    picker = picker.replace("std::strlen(old) * 6", "get_utf8_string_width(old, previewFontSize)")
    picker = picker.replace(", 8, editing)", ", previewFontSize, editing)")
    picker = picker.replace("p.y + 4 +", "p.y + (16 - previewFontSize) / 2 +")
    (components / "settings_option_picker.h").write_text(picker, encoding="utf-8")
    (components / "ble_controls.h").write_text(
        "#pragma once\nnamespace Crabit { namespace CompanionBle {\n"
        "inline bool active() { return false; }\n"
        'inline const char* status() { return previewChinese ? "蓝牙已关闭" : "BLE OFF"; }\n'
        "} inline void toggleBleFromUi() {} }\n",
        encoding="utf-8",
    )
    (OUT / "sys_config.h").write_text(
        "#pragma once\nstruct Config { float brightness=0.6f; bool invert_disp=false; "
        "uint32_t screen_off_ms=10000,deep_sleep_ms=30000; uint8_t watch_face=0; };\n"
        "namespace Crabit { inline uint8_t validWatchFace(uint8_t v) { return v<3?v:0; } }\n"
        "inline Config sys_cfg; struct ConfigManager { enum class SaveResult { Saved }; "
        "static SaveResult save() { return SaveResult::Saved; } };\n",
        encoding="utf-8",
    )
    return source + action + picker + "".join(translations.values())


def generate_glyphs(parsers, text):
    chars = set(chr(code) for code in range(32, 127))
    for literal in re.findall(r'"((?:[^"\\]|\\.)*)"', text):
        chars.update(char for char in literal if ord(char) >= 128)
    blob = bytearray()
    index = []
    for size, parser in parsers.items():
        for char in sorted(chars):
            glyph = parser.get_glyph(ord(char), size)
            if glyph is None:
                raise ValueError(f"Missing native {size}px glyph {char!r}; no TTF fallback")
            data, width = glyph
            index.append((ord(char), size, width, len(blob)))
            blob.extend(data)
    for char in "0123456789: ":
        data, width = parsers[12].get_glyph(ord(char), 12)
        image = Image.new("1", (width, 12))
        stride = (width + 7) // 8
        for y in range(12):
            for x in range(width):
                image.putpixel((x, y), bool(data[y * stride + x // 8] & (128 >> (x % 8))))
        image = image.resize((width * 2, 24), Image.Resampling.NEAREST)
        packed = bytearray()
        for y in range(24):
            for byte in range((width * 2 + 7) // 8):
                value = 0
                for bit in range(8):
                    x = byte * 8 + bit
                    if x < width * 2 and image.getpixel((x, y)):
                        value |= 128 >> bit
                packed.append(value)
        index.append((ord(char), 24, width * 2, len(blob)))
        blob.extend(packed)
    source = "#include <NanoUI/product/hal/font_engine.h>\n#include <NanoUI/product/core/utf8.h>\n"
    source += "struct Glyph { uint32_t code; uint8_t size,width; uint32_t offset; };\n"
    source += "static const uint8_t bits[]={" + ",".join(map(str, blob)) + "};\n"
    source += (
        "static const Glyph glyphs[]={" + ",".join("{%s,%s,%s,%s}" % row for row in index) + "};\n"
    )
    source += "uint32_t decode_utf8(const char** p) { return NanoUI::detail::decodeUtf8(p); }\n"
    source += (
        "const uint8_t* get_chinese_glyph(uint32_t code,uint8_t size,uint8_t* width,uint8_t) {"
    )
    source += "for (const auto& g:glyphs) if(g.code==code&&g.size==size) {if(width)*width=g.width;return bits+g.offset;} return nullptr;}\n"
    source += "int16_t get_utf8_string_width(const char* p,uint8_t size,uint8_t font) {"
    source += "int n=0;while(p&&*p){uint8_t w=0;auto c=decode_utf8(&p);get_chinese_glyph(c,size,&w,font);n+=w?w:size;}return n;}\n"
    (OUT / "review_fonts.cpp").write_text(source, encoding="utf-8")
    (OUT / "font-coverage.json").write_text(
        json.dumps(
            {
                "font_release": FONT_TAG,
                "font_archive_sha256": FONT_HASHES,
                "sizes": [8, 10, 12],
                "characters": "".join(sorted(chars)),
                "missing_glyphs": [],
                "small_font_ttf_fallback": False,
                "large_digits": "12px BDF nearest-neighbor integer 2x",
                "glyph_count": len(index),
                "bitmap_bytes": len(blob),
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    print(
        f"Native glyph subset: {len(index)} glyphs, {len(blob)} bitmap bytes; no small-font fallback"
    )


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    parsers = fonts()
    text = stage_settings()
    for header in ("watch_pages.h", "animated_faces.h", "app_pages.h"):
        text += (SOURCES / header).read_text(encoding="utf-8")
    generate_glyphs(parsers, text)
    compiler = shutil.which("g++")
    if not compiler:
        raise RuntimeError("Existing host g++ required; no installation attempted")
    renderer = OUT / "render-review.exe"
    command = [
        compiler,
        "-std=c++17",
        "-O0",
        "-g",
        "-I",
        str(OUT),
        "-I",
        str(SOURCES),
        "-I",
        str(ROOT / "libraries/NanoUI/src"),
        "-I",
        str(ROOT / "firmware/stm32/Core/NanoUI/include"),
        str(SOURCES / "preview_main.cpp"),
        str(OUT / "review_fonts.cpp"),
        str(ROOT / "libraries/NanoUI/src/NanoUI/product/runtime.cpp"),
        "-o",
        str(renderer),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(renderer), str(OUT)], check=True)
    labels = {
        "settings-en": "设置 · 产品布局 · 8px（离线）",
        "settings-8-top": "设置 · 原生8px中文 · 顶部",
        "settings-10-top": "设置 · 原生10px中文 · 顶部",
        "settings-8-bottom": "设置 · 原生8px中文 · 底部",
        "settings-10-bottom": "设置 · 原生10px中文 · 底部",
        "settings-8-actions": "设置 · 原生8px中文 · 操作按钮",
        "settings-10-actions": "设置 · 原生10px中文 · 操作按钮",
        "weather-stale": "天气 · 保留过期缓存",
        "weather-empty": "天气 · 尚未同步",
        "weather-selected-8": "天气 · 已选8px正文",
        "tools-0-8": "蓝牙中心 · 8px排版候选（静态图替代动图）",
        "tools-1-8": "网络审计 · 8px排版候选（静态图替代动图）",
        "tools-2-8": "扫描方式 · 8px排版候选",
        "tools-3-8": "网络列表 · 8px模拟数据",
    }
    faces = ["信息表盘", "极简数字", "天气表盘", "指针表盘"]
    for size in (8, 10, 12):
        for face, name in enumerate(faces):
            labels[f"face-{face}-{size}"] = f"{name} · {size}px"
        for style in range(2):
            labels[f"weather-{style}-{size}"] = f"天气布局{style+1} · {size}px"
    rows = []
    for name, label in labels.items():
        path = OUT / f"{name}.pgm"
        with Image.open(path) as image:
            image.convert("RGB").save(OUT / f"{name}.bmp")
        rows.append((label, OUT / f"{name}.bmp"))
    motions = []
    for variant, title in enumerate(("点阵流体时间", "极简滚动时间")):
        snapshots = []
        for frame in range(80):
            pgm = OUT / f"motion-{variant}-{frame:03d}.pgm"
            with Image.open(pgm) as image:
                image.convert("RGB").save(pgm.with_suffix(".bmp"))
                snapshots.append(image.copy().resize((512, 256), Image.Resampling.NEAREST))
        snapshots[0].save(
            OUT / f"motion-{variant}.gif",
            save_all=True,
            append_images=snapshots[1:],
            duration=70,
            loop=0,
            optimize=False,
        )
        motions.append(
            f"{title} · 循环播放（模拟时间）\t{OUT / f'motion-{variant}-{{frame}}.bmp'}\t80\t70\n"
        )
        rows.append((title + " · 动效中间帧", OUT / f"motion-{variant}-030.bmp"))
    (OUT / "manifest.tsv").write_text(
        "".join(motions) + "".join(f"{label}\t{path}\n" for label, path in rows[:-2]),
        encoding="utf-8",
    )
    # A separate executable keeps the previous review window usable while rebuilding.
    viewer = OUT / "watch-ui-motion-review.exe"
    subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-O2",
            "-mwindows",
            "-DCRABIT_UI_DESIGN_REVIEW",
            str(ROOT / "tools/windows_ui_preview.cpp"),
            "-o",
            str(viewer),
            "-lgdi32",
        ],
        check=True,
    )
    font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)
    sheet = Image.new("RGB", (1008, ((len(rows) + 2) // 3) * 230), "#eeeeee")
    draw = ImageDraw.Draw(sheet)
    for i, (label, path) in enumerate(rows):
        x, y = (i % 3) * 336 + 12, (i // 3) * 230 + 10
        draw.text((x, y), label, font=font, fill="#202020")
        with Image.open(path) as image:
            sheet.paste(image.resize((256, 128), Image.Resampling.NEAREST), (x, y + 32))
    sheet.save(OUT / "contact-sheet.png")
    strip = Image.new("RGB", (6 * 256, 2 * 160), "#eeeeee")
    strip_draw = ImageDraw.Draw(strip)
    for variant in (0, 1):
        for column, frame in enumerate((0, 24, 28, 32, 36, 39)):
            strip_draw.text(
                (column * 256 + 4, variant * 160),
                f"{'点阵流体' if variant == 0 else '极简滚动'} · {frame}",
                font=font,
                fill="#202020",
            )
            with Image.open(OUT / f"motion-{variant}-{frame:03d}.bmp") as image:
                strip.paste(
                    image.resize((256, 128), Image.Resampling.NEAREST),
                    (column * 256, variant * 160 + 28),
                )
    strip.save(OUT / "motion-strip.png")
    print(viewer)


if __name__ == "__main__":
    main()
