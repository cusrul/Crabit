"""UI-only contracts for the BW16 beacon readability pass."""

from pathlib import Path
import re
import shutil
import subprocess
import unittest
from PIL import Image

from tools.tests.test_bw16_product_fonts import ASSET, glyphs
from tools import render_bw16_small_fonts as preview

ROOT = Path(__file__).resolve().parents[2]
BW = ROOT / "firmware/rtl8720dn/NanoUI"

DRIVER = r"""
#include <NanoUI/product/hal/canvas.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
class String {
 std::string v;
 public: String()=default; String(const char*s):v(s?s:""){} String(const std::string&s):v(s){}
 String substring(size_t n)const{return String(v.substr(n));} const char*c_str()const{return v.c_str();}
 friend String operator+(const String&a,const char*b){return String(a.v+(b?b:""));}
};
struct WiFiResult { String ssid; int channel; };
struct FakeAP { uint8_t mac[6]; char ssid[33]; int channel; };
struct Context { std::vector<WiFiResult> scanResults; };
static Context context; static Context* g_hackCtx=&context;
static int DEEP_SCAN_CHANNELS[]={1,6,11}; static int scan_ch_idx=1,radarOffset=0,radarIndex=0,tacticIndex=0;
static constexpr int STATE_MODE_SEL=0,STATE_LIVE_RADAR=1,STATE_TACTIC=2,STATE_ATTACK=3;
static unsigned long anim_frame=123; static FakeAP global_spam_list[60]; static int spam_index=0;
static const char* tacticItems[]={"1. 随机名称","2. 预设名称","3. 名称克隆"};
int scan_mode=1;
static char buf[128];
class PreviewCanvas final:public Canvas {
 public: uint8_t pixels[128*64]{}; bool badTextSize=false,badPixel=false,badTextOverlap=false; int badX=0,badY=0;
 std::vector<Rect> textRects;
 void clear()override{std::memset(pixels,0,sizeof(pixels));textRects.clear();} void display()override{}
 int16_t width()override{return 128;} int16_t height()override{return 64;}
 void drawText(int16_t x,int16_t y,const char*t,uint8_t s=12,bool inv=false,uint8_t f=0)override{
   for(const unsigned char*p=(const unsigned char*)t;*p;++p)if(*p>=128&&s!=8)badTextSize=true;
   Rect visible=Rect{x,y,NanoCoordinate(x+get_utf8_string_width(t,s,f)),NanoCoordinate(y+s)}
       .intersection(getClipRect()).intersection({0,0,128,64});
   if(visible.right>visible.left&&visible.bottom>visible.top) {
     for(const auto& previous:textRects) if(visible.intersects(previous)) {
       badTextOverlap=true;fprintf(stderr,"overlapping text: %s at %d,%d\n",t,x,y);
     }
     textRects.push_back(visible);
   }
   Canvas::drawText(x,y,t,s,inv,f);
 }
 void drawPixel(int16_t x,int16_t y,uint16_t c)override{
   if(x<0||x>=128||y<0||y>=64){badPixel=true;badX=x;badY=y;return;}
   auto&p=pixels[y*128+x];if(c==C_INVERT)p^=255;else p=c==C_WHITE?255:0;
 }
 bool save(const char*p){FILE*f=fopen(p,"wb");if(!f)return false;fprintf(f,"P5\n128 64\n255\n");bool ok=fwrite(pixels,1,sizeof(pixels),f)==sizeof(pixels);return fclose(f)==0&&ok&&!badTextOverlap;}
};
@BEACON_HELPER@
void beaconPaint(Canvas*canvas,int state){ @BEACON_PAINT@ }
int main(int argc,char**argv){if(argc!=2)return 2; PreviewCanvas c; char p[512];
 std::strcpy(global_spam_list[0].ssid,"Cafe_12345678901234567890");
 context.scanResults={{"长中文网络SSID示例abcdef",1},{"Cafe_123456789012345",6},{"末尾网络名称",11},{"ExtraNet",3}};
 const char*bn[]={"beacon-mode","beacon-radar-empty","beacon-radar-first","beacon-radar-first-network","beacon-radar-last","beacon-tactic-first","beacon-tactic-last","beacon-attack"};
 int states[]={0,1,1,1,1,2,2,3}; int indexes[]={0,0,0,1,4,0,2,0}; int offsets[]={0,0,0,0,2,0,0,0};
 for(int i=0;i<8;++i){radarIndex=indexes[i];radarOffset=offsets[i];tacticIndex=i==6?2:0;if(i==1)context.scanResults.clear();else if(i==2)context.scanResults={{"长中文网络SSID示例abcdef",1},{"Cafe_123456789012345",6},{"末尾网络名称",11},{"ExtraNet",3}};c.clear();beaconPaint(&c,states[i]);
  snprintf(p,sizeof(p),"%s/%s.pgm",argv[1],bn[i]);if(!c.save(p)||c.badTextSize||c.badPixel){fprintf(stderr,"%s bad-size=%d bad-pixel=%d at %d,%d\\n",bn[i],c.badTextSize,c.badPixel,c.badX,c.badY);return 10+i;}}
}
"""


def extracted_renderer(beacon: str) -> str:
    # Include the exact production row helper and complete state painter branches.
    helper = preview.block(beacon, "void drawArsenalItem(")
    body = beacon[beacon.index("// ================== UI 渲染 ==================") :]
    body = body[body.index("if (state == STATE_MODE_SEL)") : body.index("canvas->display();")]
    # Preserve the source's state chain but strip its outer `else` prefix only through
    # direct invocation with states 0..3; all rendering statements remain verbatim.
    return DRIVER.replace("@BEACON_HELPER@", helper).replace("@BEACON_PAINT@", body)


class BeaconReadableUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.beacon = (BW / "app/app_wifi_beacon.h").read_text(encoding="utf-8")

    def test_old_ghost_removed_and_bluetooth_center_preserved(self):
        tools = (BW / "app/app_wifi_tools.h").read_text(encoding="utf-8")
        self.assertFalse((BW / "app/app_ble_ghost.h").exists())
        self.assertNotIn("AppBleGhost", tools)
        self.assertNotIn("app_ble_ghost.h", tools)
        launcher = (BW / "app/launcher.h").read_text(encoding="utf-8")
        self.assertIn("AppBleSpam", launcher)
        self.assertTrue((BW / "app/app_ble_spam.h").is_file())

    def test_static_chinese_canvas_labels_have_product_8px_glyphs(self):
        source = self.beacon
        labels = set()
        for literal, size in re.findall(r'drawText\([^;]*?"([^"\\]*)"[^;]*?,\s*(\d+)', source):
            if int(size) == 8:
                labels.update(c for c in literal if ord(c) > 127)
        records = glyphs((ROOT / ASSET).read_text(encoding="utf-8"))
        missing = sorted(c for c in labels if (ord(c), 8, 0) not in records)
        self.assertFalse(missing, f"missing generated 8px glyphs: {missing}")
        self.assertTrue(labels, "test must cover Chinese labels")

    def test_beacon_uses_three_complete_rows_and_keeps_ssid_utf8_whole(self):
        self.assertIn("for (int i = 0; i < 3; i++)", self.beacon)
        self.assertIn("radarIndex >= radarOffset + 3", self.beacon)
        self.assertIn("res.ssid.c_str(), 8", self.beacon)
        self.assertNotIn("ssid.substring", self.beacon)

    def test_render_changes_leave_radio_and_input_logic_untouched(self):
        # Strip only canvas painter and the explicitly permitted list-window delta.
        def contract(text, is_beacon):
            if is_beacon:
                helper = preview.block(text, "void drawArsenalItem(")
                text = text.replace(helper, "/* authorized text helper */", 1)
            begin = text.index("// ================== UI 渲染 ==================")
            if is_beacon:
                end = text.index("// ================== 发包核心 ==================", begin)
                text = text.replace(
                    "radarIndex >= radarOffset + 3) radarOffset = radarIndex - 2",
                    "radarIndex >= radarOffset + 4) radarOffset = radarIndex - 3",
                )
            else:
                end = text.index("safe_delay(20);", begin) + len("safe_delay(20);")
            text = text[:begin] + "/* authorized paint block */" + text[end:]
            return "\n".join(line.rstrip() for line in text.splitlines() if "canvas->" not in line)

        import subprocess

        base = "3cc656910a9314fe50cd4507ebe393036d7df24f"
        for rel, current, is_beacon in (
            ("firmware/rtl8720dn/NanoUI/app/app_wifi_beacon.h", self.beacon, True),
        ):
            baseline = subprocess.check_output(
                ["git", "show", f"{base}:{rel}"], cwd=ROOT, text=True, encoding="utf-8"
            )
            self.assertEqual(
                contract(baseline, is_beacon),
                contract(current, is_beacon),
                f"non-render app contract changed: {rel}",
            )

    def test_production_canvas_painters_render_all_states_and_edges(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "host g++ required; no SDK/build is run")
        out = ROOT / "build/bw16-readability-preview/beacon"
        out.mkdir(parents=True, exist_ok=True)
        driver = extracted_renderer(self.beacon)
        cpp = out / "beacon_render.cpp"
        exe = out / "beacon_render.exe"
        cpp.write_text(driver, encoding="utf-8")
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
                str(cpp),
                str(BW / "src/font_engine.cpp"),
                str(BW / "src/nanoui_assets.cpp"),
                "-o",
                str(exe),
            ],
            check=True,
        )
        subprocess.run([str(exe), str(out)], check=True)
        names = (
            "beacon-mode",
            "beacon-radar-empty",
            "beacon-radar-first",
            "beacon-radar-first-network",
            "beacon-radar-last",
            "beacon-tactic-first",
            "beacon-tactic-last",
            "beacon-attack",
        )
        for name in names:
            native = out / f"{name}.pgm"
            self.assertTrue(native.is_file(), f"missing production paint render: {name}")
            image = Image.open(native).convert("L")
            self.assertEqual(image.size, (128, 64))
            self.assertGreater(sum(value > 0 for value in image.getdata()), 75, name)
            image.save(out / f"{name}-native.png")
            image.resize((512, 256), Image.Resampling.NEAREST).save(out / f"{name}-4x.png")


if __name__ == "__main__":
    unittest.main()
