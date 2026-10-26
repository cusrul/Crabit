"""Host-execute the actual bench input script; no SDK/radio behavior is mocked as proof."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / "firmware/rtl8720dn/diagnostics/BW16WifiAppBench/BW16WifiAppBench.ino"

PRELUDE = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
struct Output {
    template<class T> void print(T) {}
    template<class T> void println(T) {}
} Serial;
enum EventType { EVT_NONE, EVT_UP, EVT_DOWN, EVT_LEFT, EVT_RIGHT, EVT_OK, EVT_BACK };
struct UIEvent { EventType type; int param; };
class InputDriver { public: virtual void init() = 0; virtual UIEvent poll() = 0; };
enum class Page { Unknown, Mode, Diagnostic, List, Forbidden };
Page visiblePage = Page::Unknown;
bool is_display_active = true;
int displayedCount = -1;
uint32_t displayedFrames = 0;
struct Context { int currentMode = 0; } context;
Context* g_hackCtx = &context;
struct Counts { uint32_t callbacks = 0, beacons = 0, added = 0; };
Counts wifiSniffRxSnapshot() { return {}; }
int actualChannel = 1;
int wifi_get_channel(int* channel) { *channel = actualChannel; return 0; }
constexpr int NUM_DEEP_CHANNELS = 38;
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
"""

DRIVER = r"""
UIEvent tick(BenchInput& input, uint32_t elapsed, Page page) {
    clockMs += elapsed;
    visiblePage = page;
    UIEvent event = input.poll();
    require(event.type != EVT_OK || page == Page::Mode || page == Page::Diagnostic,
            "OK must never reach list/task selection");
    return event;
}
void returnToMode(BenchInput& input) {
    require(tick(input, 25001, Page::List).type == EVT_BACK, "exit list");
    require(tick(input, 200, Page::Mode).type == EVT_BACK, "exit mode selector");
    require(input.finished, "finished must be set before runExclusive returns");
}
int main(int argc, char** argv) {
    require(argc == 2, "scenario required");
    BenchInput input;
    const bool native = std::strncmp(argv[1], "native", 6) == 0;
    input.begin(native);
    if (std::strcmp(argv[1], "timeout") == 0) {
        require(tick(input, 70001, Page::Unknown).type == EVT_NONE, "timeout emits no key");
        require(!is_display_active && !input.finished, "timeout must abort");
        return 0;
    }
    if (std::strcmp(argv[1], "forbidden") == 0) {
        require(tick(input, 200, Page::Forbidden).type == EVT_NONE, "forbidden emits no key");
        require(!is_display_active && !input.finished, "task menu must abort");
        return 0;
    }
    if (native) {
        require(tick(input, 200, Page::Mode).type == EVT_OK, "native starts with default selection");
        if (std::strcmp(argv[1], "native_failure") == 0) {
            require(tick(input, 5000, Page::Diagnostic).type == EVT_NONE, "native fallback emits no key");
            require(!is_display_active && !input.finished, "native fallback must not pass as passive");
            return 0;
        }
        require(tick(input, 5000, Page::List).type == EVT_NONE, "native completion enters list");
        returnToMode(input);
        return 0;
    }
    require(tick(input, 200, Page::Mode).type == EVT_UP, "select passive");
    require(tick(input, 200, Page::Mode).type == EVT_OK, "start passive");
    tick(input, 200, Page::Diagnostic);
    if (std::strcmp(argv[1], "tune_failure") == 0) {
        for (int i = 0; i < NUM_DEEP_CHANNELS; ++i)
            require(tick(input, 200, Page::Diagnostic).type == EVT_DOWN, "bounded tune");
        require(tick(input, 200, Page::Diagnostic).type == EVT_NONE, "abort after full table");
        require(!is_display_active && !input.finished, "failed tuning must abort");
        return 0;
    }
    actualChannel = 40;
    tick(input, 200, Page::Diagnostic);
    require(tick(input, 7999, Page::Diagnostic).type == EVT_NONE, "full capture budget");
    require(tick(input, 200, Page::Diagnostic).type == EVT_OK, "diagnostic to list");
    tick(input, 200, Page::List);
    returnToMode(input);
    return 0;
}
"""


class BenchScriptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("g++")
        if compiler is None:
            raise unittest.SkipTest("host g++ unavailable")
        source = SKETCH.read_text(encoding="utf-8")
        start, end = (
            "class BenchInput : public InputDriver {",
            "class BenchRoot : public Application {",
        )
        if source.count(start) != 1 or source.count(end) != 1:
            raise AssertionError("bench script boundaries must match exactly once")
        script = source.split(start, 1)[1].split(end, 1)[0]
        cls.temp = tempfile.TemporaryDirectory(prefix="wifi-app-bench-script-")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        cpp = directory / "script.cpp"
        cpp.write_text(PRELUDE + start + script + DRIVER, encoding="utf-8")
        cls.binary = directory / "script.exe"
        subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(cls.binary)], check=True)

    def test_script_scenarios(self):
        for scenario in (
            "passive",
            "native",
            "native_failure",
            "timeout",
            "forbidden",
            "tune_failure",
        ):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.binary), scenario], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
