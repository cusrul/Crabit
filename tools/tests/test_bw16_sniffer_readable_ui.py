"""Offline render and source-contract checks for the BW16 sniffer App.

Compiles the exact state branches from app_wifi_sniff.h against the product
Canvas/font engine and current BW16 assets. Scan behavior is checked structurally;
it never runs the App or Realtek SDK.
"""

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
#include <vector>
using std::size_t;
class String {
 std::string v;
 public: String()=default; String(const char*s):v(s?s:""){} String(const std::string&s):v(s){}
 String substring(size_t n) const { return String(v.substr(n)); }
 String substring(size_t a,size_t b) const { return String(v.substr(a,b-a)); }
 const char* c_str() const { return v.c_str(); }
 String& operator+=(const char*s){v+=s?s:"";return *this;}
 friend String operator+(const char*a,const String&b){return String(std::string(a?a:"")+b.v);}
 friend String operator+(const String&a,const String&b){return String(a.v+b.v);}
};
struct WiFiResult { String ssid; int channel=1; bool selected=false; };
struct Context { int currentChannel=11,currentMode=0; unsigned long txCount=123,rxCount=456; bool isHopping=true;
 std::vector<WiFiResult> scanResults; std::vector<String> intelLogs; };
static Context context; static Context* g_hackCtx=&context;
static const char* menuItems[8]={"1. 单目标断连","2. 多目标断连","3. 静默监听","4. 探测请求","5. 信标帧","6. 断连帧","7. 伙伴设备","8. 全部帧"};
static std::vector<int> targetChannels{1,6,11}; static unsigned long currentTacticalInterval=3000;
static int scan_mode=1,radarOffset=0,radarIndex=0,menuIndex=0; static unsigned anim_frame=7;
static int scanOnResult=0,scanPromiscResult=0,scanPromiscEnabled=1;
static int scanRequestedChannel=165,scanDriverChannel=165,scanSetResult=0,scanGetResult=0;
static const char* scanFeedback=nullptr;
struct WifiSniffRxSnapshot { unsigned long callbacks=99999,frames=99999,beacons=9999,probeResponses=9999,added=100; };
static WifiSniffRxSnapshot wifiSniffRxSnapshot(){return {};}
static int map(int x,int a,int b,int c,int d){return b==a?c:c+(x-a)*(d-c)/(b-a);}
@MARQUEE_HELPER@
class PreviewCanvas final:public Canvas {
 public: uint8_t pixels[128*64]{};
 bool badNonAsciiSize=false; bool outOfBoundsPixel=false;
 void clear()override{std::memset(pixels,0,sizeof(pixels));} void display()override{}
 int16_t width()override{return 128;} int16_t height()override{return 64;}
 void drawText(int16_t x,int16_t y,const char*t,uint8_t size=12,bool inv=false,uint8_t fid=0)override{for(const unsigned char*p=(const unsigned char*)t;*p;++p)if(*p>=128&&size!=8&&size!=12)badNonAsciiSize=true;Canvas::drawText(x,y,t,size,inv,fid);}
 void drawPixel(int16_t x,int16_t y,uint16_t color)override{if(x<0||x>=128||y<0||y>=64){outOfBoundsPixel=true;return;}pixels[y*128+x]=color==C_WHITE?255:color==C_INVERT?(pixels[y*128+x]^255):0;}
 bool save(const char*p){FILE*f=std::fopen(p,"wb");if(!f)return false;std::fprintf(f,"P5\n128 64\n255\n");bool ok=std::fwrite(pixels,1,sizeof(pixels),f)==sizeof(pixels);return std::fclose(f)==0&&ok;}
};
void paint(PreviewCanvas*canvas,int state){char buf[128]{}; canvas->clear(); @BRANCHES@ canvas->display();}
int render(PreviewCanvas&c,const char*out,int state,int scan,int menu,int radar,int offset,unsigned frame,bool hopping,bool oneTarget,unsigned interval){
 scan_mode=scan;menuIndex=menu;radarIndex=radar;radarOffset=offset;anim_frame=frame;context.isHopping=hopping;targetChannels=oneTarget?std::vector<int>{1}:std::vector<int>{1,6,11};currentTacticalInterval=interval;
 paint(&c,state);char path[512];std::snprintf(path,sizeof(path),"%s.pgm",out);if(!c.save(path)||c.badNonAsciiSize||c.outOfBoundsPixel){std::fprintf(stderr,"render failed %s: smallChinese=%d outside=%d\n",out,c.badNonAsciiSize,c.outOfBoundsPixel);return 1;}return 0;
}
int main(int argc,char**argv){if(argc!=2)return 2;
 context.scanResults={{String("网络信道网络信道"),6,false},{String("网络信道"),11,true},{String("网络"),1,false},{String("信道网络"),9,false}};
 context.intelLogs={String("网络信道网络信道"),String("信道网络信道网络"),String("网络信道")};
 PreviewCanvas c; struct Case{const char*n;int s,scan,menu,radar,offset;unsigned frame;bool hop,one;unsigned interval;};
 Case cases[]={
 {"mode-0",0,0,0,0,0,7,true,false,3000},{"mode-1",0,1,0,0,0,7,true,false,3000},
 {"radar-first",1,1,0,0,0,7,true,false,3000},{"radar-last",1,1,0,4,2,7,true,false,3000},
 {"menu-first",2,1,0,0,0,7,true,false,3000},{"menu-last",2,1,7,7,0,7,true,false,3000},
 {"exec-log-window-0",3,1,2,0,0,7,true,false,3000},{"exec-log-window-1",3,1,2,0,0,87,true,false,3000},
 {"exec-locked",3,1,2,0,0,7,false,false,3000},{"exec-single-target",3,1,2,0,0,7,true,true,3000},
 {"exec-stop-footer",3,1,7,0,0,7,true,false,3000},{"exec-fast-hop-footer",3,1,2,0,0,7,true,false,200},
 {"diag-status",4,0,0,0,0,7,false,false,3000},{"diag-counts",4,0,0,0,0,87,false,false,3000},
 {"diag-failure",4,0,0,0,0,7,false,false,3000},
 {"scan-timeout",0,1,0,0,0,7,false,false,3000},{"scan-cancel",0,1,0,0,0,7,false,false,3000}};
 for(const auto&v:cases){
 scanFeedback=nullptr;
 if(std::strcmp(v.n,"diag-failure")==0){scanOnResult=-1;scanPromiscResult=-9;scanPromiscEnabled=0;scanGetResult=-1;scanSetResult=-1;scanDriverChannel=0;}
 if(std::strcmp(v.n,"scan-timeout")==0)scanFeedback="SCAN TIMEOUT";
 if(std::strcmp(v.n,"scan-cancel")==0)scanFeedback="SCAN CANCEL";
 if(render(c,(std::string(argv[1])+"/"+v.n).c_str(),v.s,v.scan,v.menu,v.radar,v.offset,v.frame,v.hop,v.one,v.interval))return 3;
 }}
"""


def extracted_driver(source: str) -> str:
    helper = preview.block(source, "void drawMarqueeText(")
    paint = source[source.index("uint32_t ui_throttle_ms") :]
    state_names = (
        "STATE_MODE_SEL",
        "STATE_LIVE_RADAR",
        "STATE_MENU",
        "STATE_EXEC",
        "STATE_SCAN_DIAG",
    )
    blocks = [
        (
            preview.block(paint, f"if (state == {name})")
            if i == 0
            else preview.block(paint, f"else if (state == {name})")
        )
        for i, name in enumerate(state_names)
    ]
    for block in blocks:
        for forbidden in ("wifi_", "safe_delay", "input->", "EVT_OK", "performSystemScan"):
            if forbidden in block:
                raise ValueError(f"Non-paint behavior found in extracted block: {forbidden}")
    # Independent state dispatch retains each complete production branch.
    code = "\n".join(f"if(state=={i}) {{\n{b[b.index('{')+1:-1]}\n}}" for i, b in enumerate(blocks))
    return DRIVER.replace("@MARQUEE_HELPER@", "static " + helper).replace("@BRANCHES@", code)


def unchanged_task_source(source: str) -> list[str]:
    """Keep RF task behavior frozen; scan lifecycle now has executable mock tests."""
    events = source[: source.index("uint32_t ui_throttle_ms")]
    signatures = (
        "void setChannel(",
        "void processHopping(",
        "else if (state == STATE_MENU)",
        "else if (state == STATE_EXEC)",
    )
    return [preview.block(events, signature) for signature in signatures]


def live_radar_event_block(source: str) -> str:
    loop_start = source.index("while(running")
    mode_branch = source.index("if (state == STATE_MODE_SEL)", loop_start)
    radar_branch = source.index("else if (state == STATE_LIVE_RADAR)", mode_branch)
    return preview.block(source[radar_branch:], "else if (state == STATE_LIVE_RADAR)")


class SnifferReadableUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (preview.BW / "app/app_wifi_sniff.h").read_text(encoding="utf-8")
        cls.generated = extracted_driver(cls.source)

    def test_only_paint_code_is_extracted_for_all_four_states(self):
        helper = preview.block(self.source, "void drawMarqueeText(")
        self.assertEqual(
            self.generated.count("canvas->drawText") - helper.count("canvas->drawText"),
            self.source[self.source.index("uint32_t ui_throttle_ms") :].count("canvas->drawText"),
        )
        for forbidden in ("wifi_", "safe_delay", "input->", "performSystemScan"):
            self.assertNotIn(forbidden, self.generated)

    def test_chinese_text_used_by_sniffer_is_covered_by_product_fonts(self):
        records = generated_glyphs((preview.ROOT / ASSET).read_text(encoding="utf-8"))
        samples = set(
            "选择扫描方式被动监听系统开始确认键任务信道网络静默监听探测请求信标帧断连帧伙伴设备全部帧单目标多目标锁定轮换全频返回跳频间隔毫秒停止接收诊断"
        )
        for size in (8, 12):
            missing = sorted(c for c in samples if (ord(c), size, 0) not in records)
            self.assertFalse(missing, f"missing {size}px glyphs: {missing}")

    def test_rf_task_behavior_matches_pinned_baseline(self):
        baseline = preview.baseline_source()
        self.assertEqual(
            unchanged_task_source(baseline),
            unchanged_task_source(self.source),
            "RF task behavior changed outside receive diagnostics",
        )

    def test_system_scan_uses_sdk_completion_and_passive_list_keeps_sweeping(self):
        callback = preview.block(self.source, "static rtw_result_t system_scan_handler_sniff(")
        self.assertIn("scan_result->scan_complete != 0", callback)
        self.assertIn("system_scan_complete_sniff = true", callback)
        self.assertIn("memcmp(ap.bssid, res.bssid, 6)", callback)
        self.assertIn("system_scan_count_sniff++", callback)

        scan = preview.block(self.source, "bool performSystemScan(")
        self.assertIn("wifi_scan_networks(system_scan_handler_sniff, scanSession)", scan)
        self.assertIn("const bool complete = system_scan_complete_sniff", scan)
        self.assertIn("if (complete) break", scan)
        self.assertNotIn("scan_duration = 3500", scan)
        self.assertIn("system_scan_count_sniff = 0", scan)
        self.assertIn("发现网络: %u", scan)

        radar = live_radar_event_block(self.source)
        scan_logic = radar[radar.index("{") + 1 : radar.index("if (e.type == EVT_DOWN)")]
        self.assertIn("setScanChannel(DEEP_SCAN_CHANNELS[scan_channel_idx])", scan_logic)
        self.assertNotIn("if (scan_mode == 0)", scan_logic)

    def test_actual_font_engine_renders_layout_edge_cases_and_clips(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "host g++ required; no toolchain installation attempted")
        out = preview.ROOT / "build/sniffer-readable-ui"
        out.mkdir(parents=True, exist_ok=True)
        cpp = out / "sniffer_render.cpp"
        exe = out / "sniffer_render.exe"
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
        names = (
            "mode-0",
            "mode-1",
            "radar-first",
            "radar-last",
            "menu-first",
            "menu-last",
            "exec-log-window-0",
            "exec-log-window-1",
            "exec-locked",
            "exec-single-target",
            "exec-stop-footer",
            "exec-fast-hop-footer",
            "diag-status",
            "diag-counts",
            "diag-failure",
            "scan-timeout",
            "scan-cancel",
        )
        for name in names:
            pgm = out / f"{name}.pgm"
            self.assertTrue(pgm.exists(), f"scenario {name} render missing")
            img = Image.open(pgm).convert("L")
            self.assertEqual(img.size, (128, 64))
            # Avoid blank/trivially clipped output. Detailed review images are native OLED pixels.
            self.assertGreater(sum(v > 0 for v in img.getdata()), 100)
            img.resize((512, 256), Image.Resampling.NEAREST).save(out / f"{name}-4x.png")


if __name__ == "__main__":
    unittest.main()
