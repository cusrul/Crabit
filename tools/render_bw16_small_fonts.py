"""Offline font/layout comparison using the product Canvas and BW16 font data.

No firmware resource generation, embedded build, SDK calls or radio operations.
The 8px scan-mode panel is extracted from the pre-change App; 10/12px panels are
layout proposals, not screenshots of modified firmware or hardware evidence.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
BW = ROOT / "firmware/rtl8720dn/NanoUI"
BASELINE = "e22b7c1"
sys.path.insert(0, str(ROOT / "libraries/NanoUI/tools"))
from nanoui_tools.glyphs import BDFParser  # noqa: E402


def block(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    for index in range(opening + 1, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if not depth:
            return source[start : index + 1]
    raise ValueError(f"Unclosed source block: {marker}")


def baseline_source() -> str:
    """Keep the historical 8px comparison honest after the product adopts12px."""
    return subprocess.check_output(
        ["git", "show", f"{BASELINE}:firmware/rtl8720dn/NanoUI/app/app_wifi_sniff.h"],
        cwd=ROOT,
    ).decode("utf-8")


DRIVER = r"""
#include <NanoUI/product/hal/canvas.h>
#include <cstdio>
#include <cstring>
#include <string>

class PreviewCanvas final : public Canvas {
public:
    uint8_t pixels[128*64]{};
    void clear() override { std::memset(pixels,0,sizeof(pixels)); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x,int16_t y,uint16_t color) override {
        if (x<0 || x>=128 || y<0 || y>=64) return;
        auto& p=pixels[y*128+x];
        if (color==C_INVERT) p^=255;
        else p=color==C_WHITE?255:0;
    }
    bool save(const std::string& path) {
        FILE* f=std::fopen(path.c_str(),"wb");
        if (!f) return false;
        std::fprintf(f,"P5\n128 64\n255\n");
        const bool ok=std::fwrite(pixels,1,sizeof(pixels),f)==sizeof(pixels);
        return std::fclose(f)==0 && ok;
    }
};

void originalScan(Canvas* canvas) {
    constexpr int STATE_MODE_SEL=0;
    int state=STATE_MODE_SEL, scan_mode=1;
    @SCAN_BLOCK@
}

const char* rows[]={"3. 静默监听", "4. 探测请求", "5. 信标帧", "6. 断连帧"};

void proposedScan(Canvas* c,int size) {
    const int header=size+2;
    c->fillRect(0,0,128,header,C_WHITE);
    c->drawText(2,1,"选择扫描方式",size,true);
    const int first=size==12?21:24;
    const int second=size==12?36:38;
    c->drawText(6,first,"1. 被动监听",size);
    c->fillRect(0,second-1,128,size+2,C_WHITE);
    c->drawText(6,second,"2. 系统扫描",size,true);
    c->drawLine(0,50,128,50,C_WHITE);
    c->drawText(4,52,"确认键开始",size);
}

void menu(Canvas* c,int size) {
    const int header=size==8?10:size+2;
    c->fillRect(0,0,128,header,C_WHITE);
    c->drawText(2,1,"选择任务",size,true);
    const int count=size==12?3:4;
    const int start=size==12?17:14;
    const int pitch=size==12?15:12;
    for (int i=0;i<count;++i) {
        const int y=start+i*pitch;
        if (i==0) c->fillRect(0,y-1,128,size+2,C_WHITE);
        c->drawText(6,y,rows[i],size,i==0);
    }
    c->drawLine(125,start,125,61,C_WHITE);
    c->fillRect(124,start+10,3,10,C_WHITE);
}

int main(int argc,char**argv) {
    if (argc!=2) return 2;
    const std::string out=argv[1];
    PreviewCanvas canvas;
    for (int size: {8,10,12}) {
        canvas.clear();
        if (size==8) originalScan(&canvas); else proposedScan(&canvas,size);
        if (!canvas.save(out+"/scan-"+std::to_string(size)+".pgm")) return 3;
        canvas.clear(); menu(&canvas,size);
        if (!canvas.save(out+"/menu-"+std::to_string(size)+".pgm")) return 3;
    }
}
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/bw16-small-fonts")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("Existing host g++ required; no installation attempted")
    source = baseline_source()
    # The same condition exists in event handling; select only the paint region.
    paint = source[source.index("uint32_t ui_throttle_ms") :]
    scan = block(paint, "if (state == STATE_MODE_SEL)")
    if any(word in scan for word in ("wifi_", "safe_delay", "input->")):
        raise ValueError("Expected a paint-only block, not App input/radio code")
    driver = output / "font_preview.cpp"
    driver.write_text(DRIVER.replace("@SCAN_BLOCK@", scan), encoding="utf-8")
    exe = output / "font-preview.exe"
    subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-O0",
            "-DCRABIT_BW16_ENABLE_NETWORK_SYNC=0",
            "-I",
            str(ROOT / "libraries/NanoUI/src"),
            "-I",
            str(BW / "src"),
            str(driver),
            str(BW / "src/font_engine.cpp"),
            str(BW / "src/nanoui_assets.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe), str(output)], check=True)
    config = json.loads((BW / "nanoui-assets.json").read_text(encoding="utf-8"))
    font = ImageFont.truetype(config["ttf_font"], 22)
    small = ImageFont.truetype(config["ttf_font"], 17)
    board = Image.new("RGB", (1264, 576), "#edf0f4")
    draw = ImageDraw.Draw(board)
    draw.text(
        (20, 12),
        "NanoUI 实际 Canvas / 当前 BW16 字库 · 离线模拟，不是实机截图",
        font=font,
        fill="#222222",
    )
    captions = ["8px 修改前：黑体转点阵", "10px 试排：仍是黑体转换", "12px 试排：现有文泉驿点阵"]
    for col, size in enumerate((8, 10, 12)):
        x = 20 + col * 416
        draw.text((x, 52), captions[col], font=small, fill="#333333")
        for row, kind in enumerate(("scan", "menu")):
            with Image.open(output / f"{kind}-{size}.pgm") as image:
                native = image.convert("RGB")
            native.save(output / f"{kind}-{size}-native.png")
            scaled = native.resize((384, 192), Image.Resampling.NEAREST)
            scaled.save(output / f"{kind}-{size}-3x.png")
            board.paste(scaled, (x, 83 + row * 230))
        draw.text(
            (x, 514), "菜单显示：" + ("3 行" if size == 12 else "4 行"), font=small, fill="#333333"
        )
    draw.text(
        (20, 551),
        "8px 扫描页提取修改前产品代码；其余为历史排版提案。放大均为最近邻，不做平滑美化。",
        font=small,
        fill="#555555",
    )
    board.save(output / "comparison.png")
    print(f"comparison: {output / 'comparison.png'}")
    # Confirm the declared font inputs really cover the sample glyphs.
    for size, name in ((8, "5x8.bdf"), (10, "6x10.bdf"), (12, "wqy12.bdf")):
        bdf = BDFParser(str(BW / "assets/fonts/bdf" / name), alignment="center")
        samples = set("选择扫描方式被动监听系统确认键开始任务")
        hits = sum(ord(c) in bdf.glyphs for c in samples)
        print(f"{size}px {name}: sample bitmap coverage {hits}/{len(samples)}")


if __name__ == "__main__":
    main()
