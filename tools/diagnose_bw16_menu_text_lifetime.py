"""Host regression probe: detect menu text borrowed from released storage.

Uses the exact launcher factory and shared Widgets, never the SDK or hardware.
The host allocator quarantines released blocks, so no invalid memory is read.
"""

from pathlib import Path
import shutil
import subprocess

from tools.render_bw16_small_fonts import block

ROOT = Path(__file__).resolve().parents[1]
DRIVER = r"""
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
struct Allocation { void* pointer; size_t bytes; bool active; bool retained; };
static Allocation allocations[20000];
static size_t allocationCount;
static bool quarantine;
void* operator new(size_t bytes) {
    void* pointer=std::malloc(bytes?bytes:1);
    if(!pointer) throw std::bad_alloc();
    if(allocationCount>=20000) std::abort();
    allocations[allocationCount++]={pointer,bytes,true,false};
    return pointer;
}
static void release(void* pointer) noexcept {
    if(!pointer) return;
    for(size_t i=allocationCount;i>0;--i) {
        auto& item=allocations[i-1];
        if(item.pointer==pointer&&item.active) {
            item.active=false;item.retained=quarantine;
            if(!quarantine) std::free(pointer);
            return;
        }
    }
    std::free(pointer);
}
void operator delete(void* p) noexcept {release(p);}
void operator delete(void* p,size_t) noexcept {release(p);}
#include <NanoUI.h>
#define CRABIT_BW16_DIAG_SMALL_APP_SET 1
#define CRABIT_BW16_IS_PRODUCT 1
static const uint8_t icon_folder_main[32]{};
@FACTORY@
static bool releasedStorage(const char* p) {
    auto address=reinterpret_cast<uintptr_t>(p);
    for(size_t i=0;i<allocationCount;++i) {
        const auto& item=allocations[i];auto first=reinterpret_cast<uintptr_t>(item.pointer);
        if(item.retained&&!item.active&&address>=first&&address<first+item.bytes) return true;
    }
    return false;
}
static int widgetBorrowed,renderBorrowed,widgetLabels,renderLabels;
static void inspectWidget(Widget* widget) {
    if(auto* text=dynamic_cast<Text*>(widget)) {
        widgetBorrowed+=releasedStorage(text->content);
        if(!releasedStorage(text->content) &&
            (!std::strcmp(text->content,"蓝牙广播") || !std::strcmp(text->content,"立体跳跃"))) ++widgetLabels;
    }
    if(auto* single=dynamic_cast<SingleChildWidget*>(widget)) if(single->child) inspectWidget(single->child);
    if(auto* multi=dynamic_cast<MultiChildWidget*>(widget)) for(auto* child:multi->children) inspectWidget(child);
}
static void inspectRender(RenderObject* node) {
    if(auto* text=dynamic_cast<RenderText*>(node)) {
        renderBorrowed+=releasedStorage(text->text);
        if(!releasedStorage(text->text) &&
            (!std::strcmp(text->text,"蓝牙广播") || !std::strcmp(text->text,"立体跳跃"))) ++renderLabels;
    }
    node->visitChildren(inspectRender);
}
int main() {
    quarantine=true;
    Widget* menu=buildModernAnimMenu(nullptr,{"蓝牙广播","立体跳跃"},{[](){},[](){}});
    inspectWidget(menu);
    // RenderObject creation only copies pointers; no layout or paint dereferences them.
    RenderObject* render=menu->createRenderObject();inspectRender(render);
    Text literal("蓝牙广播",12);
    std::printf("Widget pointers into released storage: %d\n",widgetBorrowed);
    std::printf("RenderText pointers into released storage: %d\n",renderBorrowed);
    std::printf("literal Text points into released storage: %d\n",releasedStorage(literal.content));
    std::printf("live labels in Widget/RenderText: %d/2 %d/2\n",widgetLabels,renderLabels);
    const bool passed=@EXPECT_DANGLING@
        ? widgetBorrowed==2 && renderBorrowed==2
        : widgetBorrowed==0 && renderBorrowed==0 && widgetLabels==2 && renderLabels==2;
    delete render;delete menu;
    for(size_t i=0;i<allocationCount;++i) if(allocations[i].retained) std::free(allocations[i].pointer);
    return passed && !releasedStorage(literal.content)?0:1;
}
"""


def run_probe(factory, out, expect_dangling=False):
    out.mkdir(parents=True, exist_ok=True)
    cpp = out / "probe.cpp"
    exe = out / "probe.exe"
    cpp.write_text(
        DRIVER.replace("@FACTORY@", factory).replace(
            "@EXPECT_DANGLING@", "true" if expect_dangling else "false"
        ),
        encoding="utf-8",
    )
    cxx = shutil.which("g++")
    if not cxx:
        raise RuntimeError("Existing host compiler unavailable")
    subprocess.run(
        [
            cxx,
            "-std=c++17",
            "-O0",
            "-I",
            str(ROOT / "libraries/NanoUI/src"),
            str(cpp),
            str(ROOT / "libraries/NanoUI/src/NanoUI/product/runtime.cpp"),
            str(ROOT / "libraries/NanoUI/tests/font_stub.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )
    return subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout


def main():
    app = ROOT / "firmware/rtl8720dn/NanoUI/app/launcher.h"
    factory = block(app.read_text(encoding="utf-8"), "inline Widget* buildModernAnimMenu(")
    print(run_probe(factory, ROOT / "build/menu-text-lifetime-diagnosis"), end="")


if __name__ == "__main__":
    main()
