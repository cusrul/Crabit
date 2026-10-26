"""Render exact auxiliary-App paint code on Windows; never call SDK services."""

from pathlib import Path
import re
import shutil
import subprocess
import unittest

from PIL import Image
from tools import render_bw16_small_fonts as base
from tools.tests.test_bw16_product_fonts import glyphs

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "firmware/rtl8720dn/NanoUI/app"

PREAMBLE = r"""
#include <NanoUI.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
class String {
    std::string value;
public:
    String(const char* s="") : value(s) {}
    String(const std::string& s) : value(s) {}
    String(int i) : value(std::to_string(i)) {}
    const char* c_str() const { return value.c_str(); }
    friend String operator+(const String& a, const String& b) { return a.value+b.value; }
};
struct Loot { bool is_synced; };
struct Context { std::vector<Loot> lootPool{{false},{false},{true}}; } context;
Context* g_hackCtx = &context;
bool g_ble_uplink_active = false;
namespace AuditRelay {
const char* status(){return "已交给主控";}
unsigned stored(){return 2;}
unsigned dropped(){return 1;}
}
class PreviewCanvas : public Canvas {
public:
    uint8_t pixels[128*64]{};
    bool smallChinese=false, overflow=false;
    void clear() override { std::memset(pixels,0,sizeof pixels); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x,int16_t y,uint16_t c) override {
        if(x<0||x>=128||y<0||y>=64) {overflow=true;return;}
        auto& p=pixels[y*128+x]; p=c==C_INVERT?p^255:c==C_WHITE?255:0;
    }
    void drawText(int16_t x,int16_t y,const char* s,uint8_t size=12,bool invert=false,uint8_t f=0) override {
        for(auto* p=(const unsigned char*)s;*p;++p) if(*p>=128&&size!=8&&size!=12) smallChinese=true;
        Canvas::drawText(x,y,s,size,invert,f);
    }
    void save(const std::string& path) {
        FILE* out=std::fopen(path.c_str(),"wb");
        if(!out) std::exit(4);
        std::fprintf(out,"P5\n128 64\n255\n");
        std::fwrite(pixels,1,sizeof pixels,out); std::fclose(out);
        if(smallChinese||overflow) std::exit(5);
    }
};
"""


def segment(source, start, end):
    first = source.index(start)
    return source[first : source.index(end, first)]


def driver():
    spam = (APP / "app_ble_spam.h").read_text(encoding="utf-8")
    sync = (APP / "app_ble_sync.h").read_text(encoding="utf-8")
    hopper = (APP / "app_hopper.h").read_text(encoding="utf-8")
    tools = (APP / "app_wifi_tools.h").read_text(encoding="utf-8")
    spam_paint = segment(
        spam[spam.index("while (is_display_active)") :], "canvas->clear();", "canvas->display();"
    )
    sync_paint = segment(
        sync[sync.index("while (is_display_active)") :], "canvas->clear();", "canvas->display();"
    )
    title = base.block(
        hopper[hopper.index("void runExclusive") :], "if (state == HOPPER_STATE_TITLE)"
    )
    title_paint = title[title.index("{") + 1 : title.index("if (e.type")]
    over = base.block(hopper, "else if (state == HOPPER_STATE_OVER)")
    over_paint = over[over.index("canvas->fillRect") : over.index("if (e.type")]
    menu = base.block(tools[tools.index("class WifiToolsMenuApp") :], "Widget* buildUI()")
    icons = "\n".join(re.findall(r"static const uint8_t icon_\w+\[\]\s*=\s*\{.*?\};", tools, re.S))
    declarations = "\n".join(
        f'class {name}:public Application{{ public: const char* getAppName() override{{return "preview";}} }};'
        for name in (
            "AppWifiSniffer",
            "AppWifiBeacon",
            "AppMacRadar",
            "AppSpectrum",
            "AppBleSync",
        )
    )
    return (
        PREAMBLE
        + '\n#include "nanoui_assets.h"\n'
        + icons
        + declarations
        + "\nWidget* menu()"
        + menu[menu.index("{") :]
        + r"""
void spam(Canvas* canvas,bool busy) { """
        + spam_paint
        + r""" }
void sync(Canvas* canvas,int menu_idx,bool is_syncing) {
    int synced_count=2,total_to_sync=3;
"""
        + sync_paint
        + r""" }
void title(Canvas* canvas) { """
        + title_paint
        + r""" }
void over(Canvas* canvas) { int hiscore=123; """
        + over_paint
        + r""" }
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    auto out=std::string(argv[1]); PreviewCanvas canvas;
    spam(&canvas,false);canvas.save(out+"/ble-spam-running.pgm");
    canvas.clear();spam(&canvas,true);canvas.save(out+"/ble-spam-busy.pgm");
    for(int i=0;i<3;++i) {
        canvas.clear();g_ble_uplink_active=i!=0;sync(&canvas,i,i==1);
        canvas.save(out+"/sync-"+std::to_string(i)+".pgm");
    }
    canvas.clear();title(&canvas);canvas.save(out+"/hopper-title.pgm");
    canvas.clear();over(&canvas);canvas.save(out+"/hopper-over.pgm");
    WindowManager wm(&canvas,nullptr);wm.push(menu());
    for(int i=0;i<90;++i) {wm.markDirty();wm.drawFrame();}
    canvas.save(out+"/tool-menu.pgm");wm.clear();
}
"""
    )


class AuxUiTests(unittest.TestCase):
    def test_aux_static_chinese_has_existing_product_glyphs(self):
        records = glyphs(
            (ROOT / "firmware/rtl8720dn/NanoUI/src/nanoui_assets.cpp").read_text(encoding="utf-8")
        )
        for name in ("app_ble_spam.h", "app_ble_sync.h", "app_hopper.h", "app_wifi_tools.h"):
            source = (APP / name).read_text(encoding="utf-8")
            # App IDs are not text painted by these pages (the tools folder is
            # labelled 网络审计 by its caller, not its getAppName metadata).
            source = re.sub(r"const char\* getAppName\(\) override\s*\{[^}]*\}", "", source)
            chars = {
                c
                for s in re.findall(r'"([^"\n]*)"', source)
                for c in s
                if "\u4e00" <= c <= "\u9fff"
            }
            for size in (8, 12):
                self.assertFalse(
                    [c for c in sorted(chars) if (ord(c), size, 0) not in records], name
                )

    def test_aux_non_render_sources_match_preview_baseline(self):
        for name in ("app_ble_spam.h", "app_hopper.h"):
            original = subprocess.check_output(
                ["git", "show", f"3cc6569:firmware/rtl8720dn/NanoUI/app/{name}"], cwd=ROOT
            ).decode("utf-8")
            current = (APP / name).read_text(encoding="utf-8")

            def normalized(source):
                return "\n".join(
                    line.strip()
                    for line in source.splitlines()
                    if line.strip() and "canvas->" not in line
                )

            self.assertEqual(normalized(original), normalized(current), name)

    def test_actual_aux_app_rendering(self):
        out = ROOT / "build/bw16-readability-preview/aux"
        out.mkdir(parents=True, exist_ok=True)
        cpp = out / "aux.cpp"
        exe = out / "aux.exe"
        cpp.write_text(driver(), encoding="utf-8")
        cxx = shutil.which("g++")
        self.assertIsNotNone(cxx, "Existing host C++ compiler required")
        src = ROOT / "firmware/rtl8720dn/NanoUI/src"
        subprocess.run(
            [
                cxx,
                "-std=c++17",
                "-O0",
                "-I",
                str(ROOT / "libraries/NanoUI/src"),
                "-I",
                str(src),
                str(cpp),
                str(src / "font_engine.cpp"),
                str(src / "nanoui_assets.cpp"),
                str(ROOT / "libraries/NanoUI/src/NanoUI/product/runtime.cpp"),
                "-o",
                str(exe),
            ],
            check=True,
        )
        subprocess.run([str(exe), str(out)], check=True)
        for file in out.glob("*.pgm"):
            with Image.open(file) as img:
                img.save(file.with_suffix(".png"))
                img.resize((512, 256), Image.Resampling.NEAREST).save(
                    file.with_name(file.stem + "-4x.png")
                )


if __name__ == "__main__":
    unittest.main()
