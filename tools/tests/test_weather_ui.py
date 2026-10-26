"""Host-render the production STM32 WeatherApp with its STM font resources."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "firmware/stm32/Core/NanoUI"
SHARED = ROOT / "libraries/NanoUI"
APP = UI / "app_real/app_weather.h"


def compiler() -> str | None:
    configured = os.environ.get("CXX")
    if configured:
        return shutil.which(configured) or (configured if Path(configured).is_file() else None)
    return shutil.which("g++") or shutil.which("x86_64-w64-mingw32-g++") or shutil.which("g++-14")


FAKE_MAIN = r"""#pragma once
#include <cstdint>
struct WeatherHostGlobalStore { char net_time[16] = {}; };
extern WeatherHostGlobalStore GlobalStore;
uint32_t HAL_GetTick();
namespace Crabit {
inline bool amebaSessionActive() { return false; }
namespace CompanionBle { inline bool active() { return false; } }
}
class CyberLinkApp : public Application {
public:
    enum class Mode : uint8_t { Wifi };
    explicit CyberLinkApp(Mode) {}
    const char* getAppName() override { return "HOST_WIFI"; }
};
"""


FAKE_NETWORK = r"""#pragma once
#include "weather_contract.h"
namespace Crabit { namespace NetworkWeather {
inline const char*& hostStatus() { static const char* value = "CONFIGURED"; return value; }
inline const char*& hostLabel() { static const char* value = "配置就绪"; return value; }
inline bool& hostTimeValid() { static bool value = false; return value; }
inline unsigned& hostCount() { static unsigned value = 1; return value; }
inline CrabitWeather::Day* hostDays() {
    static CrabitWeather::Day values[CrabitWeather::MaxDays] = {
        {20261020, 0, 120, 215, 10}
    };
    return values;
}
inline bool configured() { return true; }
inline bool begin() { return false; }
inline const char* status() { return hostStatus(); }
inline const char* label() { return hostLabel(); }
inline bool timeValid() { return hostTimeValid(); }
inline unsigned count() { return hostCount(); }
inline const CrabitWeather::Day& day(unsigned index) { return hostDays()[index]; }
inline const char* condition(uint16_t code) { return code == 0 ? "晴" : "多云"; }
inline unsigned phoneCount() { return hostCount(); }
inline const CrabitWeather::Day& phoneDay(unsigned index) { return hostDays()[index]; }
inline const char* phoneLabel() { return hostLabel(); }
inline uint32_t lastUpdated() { return hostTimeValid() ? 1792458000u : 0; }
inline int16_t lastUpdatedOffset() { return 480; }
} }
"""


FIXTURE = r"""#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <NanoUI/NanoUI.h>
#include "app_weather.h"
#include "network_weather.h"

WeatherHostGlobalStore GlobalStore;
static uint32_t fakeTicks = 1000;
uint32_t HAL_GetTick() { return fakeTicks; }
static uint32_t getTicks() { return fakeTicks; }

class TestHeap final : public NanoUI::MemoryAdapter {
public:
    void* alloc(size_t bytes) override { return std::malloc(bytes); }
    void free(void* value) override { std::free(value); }
};

class BitmapCanvas final : public Canvas {
public:
    uint8_t pixels[128 * 64]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x >= 0 && x < 128 && y >= 0 && y < 64) {
            uint8_t& pixel = pixels[y * 128 + x];
            if (color == C_INVERT) pixel ^= 1;
            else pixel = color == C_WHITE;
        }
    }
    bool saveBmp(const std::string& path) const {
        constexpr unsigned scale = 4, w = 128 * scale, h = 64 * scale;
        constexpr unsigned row = w * 3, bytes = 54 + row * h;
        uint8_t header[54]{};
        header[0] = 'B'; header[1] = 'M'; header[10] = 54; header[14] = 40;
        for (int i = 0; i < 4; ++i) {
            header[2 + i] = (bytes >> (8 * i)) & 255;
            header[18 + i] = (w >> (8 * i)) & 255;
            header[22 + i] = (h >> (8 * i)) & 255;
        }
        header[26] = 1; header[28] = 24;
        FILE* file = std::fopen(path.c_str(), "wb");
        if (!file) return false;
        bool ok = std::fwrite(header, 1, sizeof header, file) == sizeof header;
        for (int y = int(h) - 1; y >= 0; --y) for (unsigned x = 0; x < w; ++x) {
            const uint8_t v = pixels[(y / scale) * 128 + x / scale] ? 255 : 0;
            const uint8_t rgb[] = {v, v, v};
            ok = std::fwrite(rgb, 1, sizeof rgb, file) == sizeof rgb && ok;
        }
        return std::fclose(file) == 0 && ok;
    }
};

static RenderColumn* weatherColumn = nullptr;
static RenderRow* weatherButtons = nullptr;
static void findColumn(RenderObject* node) {
    if (!node) return;
    if (auto* column = dynamic_cast<RenderColumn*>(node)) weatherColumn = column;
    if (auto* row = dynamic_cast<RenderRow*>(node)) weatherButtons = row;
    node->visitChildren(findColumn);
}
static int absoluteX(const RenderObject* node) {
    int x = 0;
    for (auto* current = node; current; current = current->parent) x += current->pos.x;
    return x;
}
static int absoluteY(const RenderObject* node) {
    int y = 0;
    for (auto* current = node; current; current = current->parent) y += current->pos.y;
    return y;
}
static unsigned litPixels(const BitmapCanvas& canvas) {
    return std::count(canvas.pixels, canvas.pixels + sizeof canvas.pixels, uint8_t(1));
}
static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void requireChineseGlyphs(const char* text, const char* message) {
    while (*text) {
        const uint32_t codepoint = decode_utf8(&text);
        if (codepoint >= 128) {
            uint8_t width = 0;
            require(get_chinese_glyph(codepoint, 12, &width, 0) != nullptr && width > 0, message);
        }
    }
}

static void renderCase(const char* name, const char* status, const char* label,
                       bool timeValid, const char* time, unsigned days,
                       const std::string& snapshotDir) {
    std::snprintf(GlobalStore.net_time, sizeof GlobalStore.net_time, "%s", time);
    Crabit::NetworkWeather::hostStatus() = status;
    Crabit::NetworkWeather::hostLabel() = label;
    Crabit::NetworkWeather::hostTimeValid() = timeValid;
    Crabit::NetworkWeather::hostCount() = days;

    WeatherApp app;
    app.onTick();
    Widget* probeWidget = app.buildUI();
    RenderObject* probe = probeWidget->createRenderObject();
    probe->layout(BoxConstraints{128, 128, 64, 64});
    weatherColumn = nullptr;
    weatherButtons = nullptr;
    findColumn(probe);
    require(weatherColumn != nullptr, "actual WeatherApp tree contains a Column");
    require(weatherColumn->contentMainExtent == 40, "8px page lays out to 40 vertical pixels");
    require(weatherColumn->overflowMainExtent == 0, "actual WeatherApp Column has no vertical overflow");
    require(weatherButtons != nullptr, "actual WeatherApp tree contains its button Row");
    require(weatherButtons->contentMainExtent <= 128 && weatherButtons->overflowMainExtent == 0,
            "actual button Row has no horizontal overflow");
    require(weatherButtons->children.size() == 5, "button Row retains three buttons and two spacers");
    const char* buttonLabels[] = {"手机刷新", "翻页", "返回"};
    const unsigned buttonIndices[] = {0, 2, 4};
    for (unsigned i = 0; i < 3; ++i) {
        auto* button = dynamic_cast<RenderButton*>(weatherButtons->children[buttonIndices[i]]);
        require(button != nullptr && button->child != nullptr, "each expected action is a real Button");
        auto* text = dynamic_cast<RenderText*>(button->child);
        require(text != nullptr && text->textSize == 8, "each action label uses the actual 8px Text widget");
        const int left = absoluteX(button);
        const int right = left + button->size.width;
        require(left >= 0 && right <= 128, "each button's actual laid-out bounds stay inside the screen");
        require(text->size.width >= get_utf8_string_width(buttonLabels[i], 8, 0),
                "each action label's measured glyph width fits its button");
        std::printf("button-%u bounds=[%d,%d) width=%d glyph-width=%d\n", i,
                    left, right, button->size.width, get_utf8_string_width(buttonLabels[i], 8, 0));
    }
    BitmapCanvas beforeFocus;
    probe->paint(&beforeFocus, {0, 0});
    auto* probeUpdate = dynamic_cast<RenderButton*>(weatherButtons->children[0]);
    const int updateX = absoluteX(probeUpdate), updateY = absoluteY(probeUpdate);
    unsigned updateTextPixels = 0;
    for (int y = updateY; y < updateY + probeUpdate->size.height; ++y)
        for (int x = updateX; x < updateX + probeUpdate->size.width; ++x)
            updateTextPixels += beforeFocus.pixels[y * 128 + x] != 0;
    require(updateTextPixels > 0, "Update label paints before the focus overlay");
    delete probe;
    delete probeWidget;

    BitmapCanvas canvas;
    WindowManager wm(&canvas, nullptr);
    wm.push(app.buildUI(), &app);
    auto* focusedButton = dynamic_cast<RenderButton*>(wm.getFocusedRenderObject());
    require(focusedButton != nullptr && focusedButton->hasFocus,
            "initial focus belongs to an actual action Button");
    wm.drawFrame();
    const int focusX = absoluteX(focusedButton), focusY = absoluteY(focusedButton);
    unsigned focusedPixels = 0;
    for (int y = std::max(0, focusY - 2); y < std::min(64, focusY + focusedButton->size.height + 2); ++y)
        for (int x = std::max(0, focusX - 2); x < std::min(128, focusX + focusedButton->size.width + 2); ++x)
            focusedPixels += canvas.pixels[y * 128 + x] != 0;
    require(focusedPixels > 0, "initial focus paint remains visible under C_INVERT XOR semantics");
    for (int y = focusY; y < focusY + focusedButton->size.height; ++y)
        for (int x = focusX; x < focusX + focusedButton->size.width; ++x)
            require(canvas.pixels[y * 128 + x] == (beforeFocus.pixels[y * 128 + x] ^ 1),
                    "focus overlay inverts rather than erases the focused Update label");
    std::printf("%s focused-button=[%d,%d %dx%d] highlighted-pixels=%u\n", name,
                focusX, focusY, focusedButton->size.width, focusedButton->size.height, focusedPixels);
    require(litPixels(canvas) > 50, "actual NanoUI/font renderer paints the weather page");
    int firstPaintedRow = 64, lastPaintedRow = -1;
    for (int y = 0; y < 64; ++y)
        if (std::any_of(canvas.pixels + y * 128, canvas.pixels + (y + 1) * 128,
                        [](uint8_t pixel) { return pixel != 0; })) {
            firstPaintedRow = std::min(firstPaintedRow, y);
            lastPaintedRow = y;
        }
    require(firstPaintedRow >= 0 && lastPaintedRow < 64,
            "post-focus paint, including its 2px highlight expansion, stays on-screen");
    for (int row = 0; row < 5; ++row) {
        const int y0 = 2 + row * 12;
        const int y1 = y0 + 12;
        const unsigned rowPixels = std::count_if(canvas.pixels + y0 * 128,
            canvas.pixels + y1 * 128, [](uint8_t pixel) { return pixel != 0; });
        require(rowPixels > 0, "each of the five 12px page rows is visibly rendered");
    }
    if (!snapshotDir.empty())
        require(canvas.saveBmp(snapshotDir + "/" + name + ".bmp"), "write requested screenshot");
    wm.clear();
}

int main(int argc, char** argv) {
    const std::string snapshots = argc > 1 ? argv[1] : "";
    TestHeap heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(getTicks, nullptr);
    require(get_utf8_string_width("天气与校时", 8, 0) > 0, "STM generated font contains page header glyphs");
    require(get_utf8_string_width("配置就绪", 8, 0) > 0, "STM generated font contains status glyphs");
    require(get_utf8_string_width("10/20 晴 12~21度 雨10%", 8, 0) > 0,
            "STM generated font renders forecast text");
    requireChineseGlyphs("天气与校时", "STM generated font has every 8px header glyph");
    requireChineseGlyphs("配置就绪", "STM generated font has every 8px configured-status glyph");
    requireChineseGlyphs("已校时 天气超时", "STM generated font has every 8px timeout-status glyph");
    requireChineseGlyphs("10/20 晴 12~21度 雨10%", "STM generated font has every 8px forecast glyph");
    requireChineseGlyphs("手机刷新翻页返回", "STM generated font has every action-button glyph");
    requireChineseGlyphs("手机天气请在手机端刷新天气暂无更新时间暂无天气缓存上次同步超时保留缓存天气已更新",
                        "STM generated font has every phone-weather status glyph");

    renderCase("configured", "READY", "请在手机端刷新天气", false, "", 0, snapshots);
    renderCase("timeout", "STALE", "同步超时 保留缓存", true, "09:41:23", 1, snapshots);
    renderCase("forecast", "UPDATED", "天气已更新", true, "09:41:23", 1, snapshots);
    std::puts("weather-ui: production WeatherApp rendered; 3 scenarios, 40px layout, zero overflow");
    return 0;
}
"""


class WeatherUiHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cxx = compiler()
        if not cls.cxx:
            raise unittest.SkipTest(
                "No existing g++/w64 compiler found; toolchain installation is out of scope"
            )
        for path in (
            APP,
            UI / "src/font_engine.cpp",
            UI / "src/nanoui_assets.cpp",
            SHARED / "src/NanoUI/product/runtime.cpp",
        ):
            if not path.is_file():
                raise AssertionError(f"required production host-render input is missing: {path}")
        cls.temp = tempfile.TemporaryDirectory(prefix="crabit-weather-ui-")
        temp = Path(cls.temp.name)
        shutil.copyfile(APP, temp / "app_weather.h")
        if (temp / "app_weather.h").read_bytes() != APP.read_bytes():
            raise AssertionError(
                "temporary app_weather.h is not byte-identical to production source"
            )
        (temp / "main.h").write_text(FAKE_MAIN, encoding="utf-8")
        (temp / "network_weather.h").write_text(FAKE_NETWORK, encoding="utf-8")
        fixture = temp / "weather_ui_host.cpp"
        fixture.write_text(FIXTURE, encoding="utf-8")
        cls.exe = temp / "weather-ui-host.exe"
        command = [
            cls.cxx,
            "-std=c++17",
            "-O0",
            "-g",
            "-I",
            str(temp),
            "-I",
            str(UI / "include"),
            "-I",
            str(ROOT / "firmware/shared"),
            "-I",
            str(SHARED / "src"),
            str(fixture),
            str(UI / "src/font_engine.cpp"),
            str(SHARED / "src/NanoUI/product/runtime.cpp"),
            str(UI / "src/nanoui_assets.cpp"),
            "-o",
            str(cls.exe),
        ]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if result.returncode:
            raise AssertionError(
                "production WeatherApp host fixture compile failed\n"
                + result.stdout
                + result.stderr
            )
        cls.build_command = command

    @classmethod
    def tearDownClass(cls) -> None:
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def test_production_weather_app_renders_without_vertical_overflow(self) -> None:
        snapshots = os.environ.get("CRABIT_WEATHER_SNAPSHOT_DIR")
        if snapshots:
            Path(snapshots).mkdir(parents=True, exist_ok=True)
        result = subprocess.run(
            [str(self.exe), snapshots or ""], cwd=ROOT, text=True, capture_output=True
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout, end="")
        if snapshots:
            for name in ("configured", "timeout", "forecast"):
                image = Path(snapshots) / f"{name}.bmp"
                self.assertTrue(image.is_file(), f"missing requested rendered screenshot: {image}")
                self.assertEqual(image.stat().st_size, 393270, f"unexpected BMP size: {image}")
                bmp = image.read_bytes()
                self.assertEqual(bmp[:2], b"BM")
                self.assertEqual(struct.unpack_from("<ii", bmp, 18), (512, 256))
                self.assertEqual(struct.unpack_from("<H", bmp, 28)[0], 24)


if __name__ == "__main__":
    unittest.main(verbosity=2)
