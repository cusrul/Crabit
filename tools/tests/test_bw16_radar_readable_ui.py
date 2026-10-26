"""Offline host rendering of the production BW16 radar and spectrum paint code."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import unittest

from PIL import Image

from tools import render_bw16_small_fonts as preview
from tools.tests.test_bw16_product_fonts import ASSET, glyphs as generated_glyphs


DRIVER = r"""
#include <NanoUI/product/hal/canvas.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
struct RadarTarget { uint8_t mac[6]; uint32_t first_seen,last_seen,hits; bool is_random; };
static RadarTarget radar_targets[32]; static int radar_target_count=0;
static constexpr int STATE_SCAN=0,STATE_LIST=1,STATE_MONITOR=2;
static int list_offset=0,list_idx=0;
static uint8_t locked_mac[6]={0x02,0x11,0x22,0x33,0x44,0x55};
static volatile uint32_t locked_mac_last_seen=0,locked_hits_recent=10;
static float current_vals[25]{}; static float peak_vals[25]{};
static int max_activity_ch=11; static int band_mode=0;
static unsigned long now=12000;
static const int NUM_2G=14,NUM_5G=25;
static const char* title2="2.4吉赫兹频谱"; static const char* title5="5.8吉赫兹频谱";
class PreviewCanvas final:public Canvas {
public:
 uint8_t pixels[128*64]{}; bool badTextSize=false,bounds=false,textBounds=false,missingGlyph=false; unsigned textCalls=0,chineseCalls=0;
 void clear()override{std::memset(pixels,0,sizeof(pixels));} void display()override{}
 int16_t width()override{return 128;} int16_t height()override{return 64;}
 void drawText(int16_t x,int16_t y,const char*t,uint8_t size=12,bool inv=false,uint8_t fid=0)override{
  ++textCalls; bool chinese=false;const char* cursor=t;while(*cursor){const uint32_t cp=decode_utf8(&cursor);if(cp>=128){chinese=true;if(size!=8&&size!=12)badTextSize=true;uint8_t glyphWidth=0;if(!get_chinese_glyph(cp,size,&glyphWidth,fid))missingGlyph=true;}}
  if(chinese)++chineseCalls;const int w=get_utf8_string_width(t,size,fid);if(x<0||y<0||x+w>128||y+size>64){textBounds=true;fprintf(stderr,"text bbox x=%d y=%d w=%d size=%u text=%s\\n",x,y,w,size,t);}
  Canvas::drawText(x,y,t,size,inv,fid);
 }
 void drawPixel(int16_t x,int16_t y,uint16_t c)override{if(x<0||x>=128||y<0||y>=64){bounds=true;return;}pixels[y*128+x]=c==C_WHITE?255:c==C_INVERT?(pixels[y*128+x]^255):0;}
 bool save(const char*p){FILE*f=fopen(p,"wb");if(!f)return false;fprintf(f,"P5\n128 64\n255\n");bool ok=fwrite(pixels,1,sizeof(pixels),f)==sizeof(pixels);return fclose(f)==0&&ok;}
};
void radarPaint(PreviewCanvas*canvas,int state){char buf[32]; @RADAR_BRANCHES@ }
void spectrumPaint(PreviewCanvas*canvas){@SPECTRUM_PAINT@}
int render(PreviewCanvas&c,const char*out,int state,int count,bool random,unsigned char mac5,unsigned age,int band,bool empty=false,unsigned long tick=12000){
 now=tick;
 radar_target_count=count;list_idx=count?count-1:0;list_offset=count>3?count-3:0;for(int i=0;i<count;++i){radar_targets[i]={{0x02,0xAA,0xBB,0xCC,0xDD,(uint8_t)(mac5+i)},0,now-age*1000,20,random};}
 locked_mac_last_seen=now-age*1000;band_mode=band;for(int i=0;i<25;++i){current_vals[i]=(i%5)*11;peak_vals[i]=(i%7+1)*13;}
 c.resetClipRect();c.clear();if(empty)for(int i=0;i<25;++i)current_vals[i]=peak_vals[i]=0;if(state<3)radarPaint(&c,state);else spectrumPaint(&c);
 std::string p=std::string(out)+"/"+(state==0?"scan":state==1?"list-empty":state==2?"monitor":empty?"spectrum-empty":band==0?"spectrum-2g":"spectrum-5g");
 if(state==1&&count)p=std::string(out)+(count==1?"/list-first":"/list-long");
 if(state==2&&age>=10)p=std::string(out)+"/monitor-safe";
 if(state==2&&age<10)p=std::string(out)+(tick/150%2==0?"/monitor-blink-white":"/monitor-blink-black");
 bool saved=c.save((p+".pgm").c_str());if(!saved||c.badTextSize||c.bounds||c.textBounds||c.missingGlyph||!c.textCalls||!c.chineseCalls){fprintf(stderr,"render state=%d band=%d saved=%d badsize=%d bounds=%d textbounds=%d missing=%d calls=%u chinese=%u\\n",state,band,saved,c.badTextSize,c.bounds,c.textBounds,c.missingGlyph,c.textCalls,c.chineseCalls);return 1;}return 0;
}
int main(int argc,char**argv){if(argc!=2)return 2;PreviewCanvas c;
 return render(c,argv[1],0,0,false,0,0,0)||render(c,argv[1],1,0,false,0,0,0)||render(c,argv[1],1,1,true,0xF1,2,0)||render(c,argv[1],1,5,false,0xF1,31,0)||render(c,argv[1],2,1,false,0xF1,2,0,false,12000)||render(c,argv[1],2,1,false,0xF1,2,0,false,12150)||render(c,argv[1],2,1,false,0xF1,15,0)||render(c,argv[1],3,0,false,0,0,0,true)||render(c,argv[1],3,0,false,0,0,0)||render(c,argv[1],3,0,false,0,0,1);}
"""


def extracted_driver(source: str) -> str:
    radar_at = source.index("// ================== UI Professional 渲染")
    radar = source[
        radar_at : source.index("canvas->display();", radar_at) + len("canvas->display();")
    ]
    radar = radar.replace("canvas->display();", "")
    spectrum_at = source.index("// ================== UI 渲染 (专业瀑布流直方图)")
    spectrum = source[
        spectrum_at : source.index("canvas->display();", spectrum_at) + len("canvas->display();")
    ]
    spectrum = spectrum.replace("canvas->display();", "")
    for block in (radar, spectrum):
        if any(token in block for token in ("wifi_", "safe_delay", "input->", "EVT_")):
            raise ValueError("extracted paint contains radio, delay, or input behavior")
    # The production declarations/title construction are supplied as fixed fake data.
    spectrum = spectrum.replace(
        'String title = (band_mode == 0) ? "2.4吉赫兹频谱" : "5.8吉赫兹频谱";',
        "const char* title = (band_mode == 0) ? title2 : title5;",
    )
    spectrum = spectrum.replace("title.c_str()", "title")
    return DRIVER.replace("@RADAR_BRANCHES@", radar).replace("@SPECTRUM_PAINT@", spectrum)


class RadarReadableUiTests(unittest.TestCase):
    def test_non_render_source_matches_preview_baseline(self):
        import subprocess

        path = "firmware/rtl8720dn/NanoUI/app/app_mac_radar.h"
        original = subprocess.check_output(
            ["git", "show", f"3cc6569:{path}"], cwd=preview.ROOT
        ).decode("utf-8")

        def normalized(source):
            for marker in (
                "// ================== UI Professional 渲染",
                "// ================== UI 渲染 (专业瀑布流直方图)",
            ):
                start = source.index(marker)
                end = source.index("canvas->display();", start) + len("canvas->display();")
                source = source[:start] + "/* UI omitted */" + source[end:]
            return "\n".join(line.rstrip() for line in source.splitlines())

        self.assertEqual(normalized(original), normalized(self.source))

    @classmethod
    def setUpClass(cls):
        cls.source = (preview.BW / "app/app_mac_radar.h").read_text(encoding="utf-8")
        cls.generated = extracted_driver(cls.source)

    def test_chinese_literals_have_existing_12px_product_glyphs(self):
        import re

        records = generated_glyphs((preview.ROOT / ASSET).read_text(encoding="utf-8"))
        literals = re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', self.source)
        chars = {ch for value in literals for ch in value if ord(ch) > 127}
        missing = sorted(ch for ch in chars if (ord(ch), 12, 0) not in records)
        self.assertFalse(missing, f"missing 12px glyphs: {missing}")

    def test_extraction_is_paint_only(self):
        for forbidden in ("wifi_", "safe_delay", "input->", "EVT_", "promisc_callback"):
            self.assertNotIn(forbidden, self.generated)

    def test_radio_and_input_contract_literals_remain_present(self):
        # Guard the non-render contract without invoking Git in this shared worktree.
        for contract in (
            "wifi_off(); safe_delay(100)",
            "wifi_on(RTW_MODE_PROMISC)",
            "wifi_set_promisc(RTW_PROMISC_ENABLE_2, radar_promisc_callback, 1)",
            "now - scan_timer > 150",
            "now - radar_targets[i].last_seen < 60000",
            "now - hit_reset_timer > 3000",
            "EVT_BACK",
            "EVT_DOWN",
            "EVT_UP",
            "EVT_OK",
            "locked_hits_recent * 10",
            "if (threat_level > 100) threat_level = 100",
        ):
            self.assertIn(contract, self.source)

    def test_actual_canvas_and_product_font_render_cases(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "existing host g++ is required")
        out = preview.ROOT / "build/bw16-readability-preview/radar"
        out.mkdir(parents=True, exist_ok=True)
        cpp, exe = out / "radar_render.cpp", out / "radar_render.exe"
        cpp.write_text(self.generated, encoding="utf-8")
        subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-O0",
                "-DCRABIT_BW16_ENABLE_NETWORK_SYNC=0",
                "-I",
                str(preview.ROOT / "libraries/NanoUI/src"),
                "-I",
                str(preview.BW / "src"),
                str(cpp),
                str(preview.BW / "src/font_engine.cpp"),
                str(preview.BW / "src/nanoui_assets.cpp"),
                "-o",
                str(exe),
            ],
            check=True,
        )
        subprocess.run([str(exe), str(out)], check=True)
        for name in (
            "scan",
            "list-empty",
            "list-first",
            "list-long",
            "monitor-blink-white",
            "monitor-blink-black",
            "monitor-safe",
            "spectrum-empty",
            "spectrum-2g",
            "spectrum-5g",
        ):
            with Image.open(out / f"{name}.pgm") as image:
                native = image.convert("L")
                self.assertEqual(native.size, (128, 64))
                self.assertGreater(sum(p > 0 for p in native.getdata()), 100, name)
                native.save(out / f"{name}-native.png")
                native.resize((512, 256), Image.Resampling.NEAREST).save(out / f"{name}-4x.png")

    def test_alert_blink_phases_are_distinct_and_bounded(self):
        out = preview.ROOT / "build/bw16-readability-preview/radar"
        with (
            Image.open(out / "monitor-blink-white.pgm") as white,
            Image.open(out / "monitor-blink-black.pgm") as black,
        ):
            self.assertNotEqual(list(white.getdata()), list(black.getdata()))
            for image in (white, black):
                self.assertEqual(
                    image.crop((0, 0, 128, 14)).getextrema(),
                    (0, 255),
                    "alert title glyphs must contrast with the inverted header",
                )
                self.assertEqual(
                    image.crop((8, 25, 120, 39)).getextrema(),
                    (0, 255),
                    "100% threat label must render inside its bounded contrast panel",
                )


if __name__ == "__main__":
    unittest.main()
