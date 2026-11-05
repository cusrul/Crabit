#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include <NanoUI.h>
#include "app_counter.h"
#include "app_temperature.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace {
uint32_t clockMs = 1000;
uint32_t now() { return clockMs; }
bool sampleAvailable = true;
StmSensorSnapshot sample{};

class Heap : public NanoUI::MemoryAdapter {
    std::map<void*, size_t> sizes;
public:
    size_t live = 0, peak = 0;
    void* alloc(size_t n) override {
        void* p = std::malloc(n);
        if (p) { sizes[p] = n; live += n; peak = (std::max)(peak, live); }
        return p;
    }
    void free(void* p) override {
        if (p) { live -= sizes.at(p); sizes.erase(p); std::free(p); }
    }
};

class Screen : public Canvas {
public:
    std::vector<uint32_t> pixels = std::vector<uint32_t>(128 * 64, 0);
    HWND window = nullptr;
    void clear() override { std::fill(pixels.begin(), pixels.end(), 0); }
    void display() override { if (window) InvalidateRect(window, nullptr, FALSE); }
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t c) override {
        if (x < 0 || y < 0 || x >= 128 || y >= 64) return;
        auto& p = pixels[y * 128 + x];
        p = c == C_INVERT ? p ^ 0xffffff : (c == C_WHITE ? 0xffffff : 0);
    }
    bool save(const std::filesystem::path& path) {
        Gdiplus::Bitmap bitmap(512, 256, PixelFormat32bppARGB);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 512; ++x)
                bitmap.SetPixel(x, y, Gdiplus::Color(0xff000000 | pixels[(y / 4) * 128 + x / 4]));
        const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        return bitmap.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
    }
};

class Keys : public InputDriver {
public:
    EventType next = EVT_NONE;
    void init() override {}
    UIEvent poll() override { UIEvent e{next, 0}; next = EVT_NONE; return e; }
};
Screen* activeScreen = nullptr;
Keys* activeKeys = nullptr;

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM key, LPARAM param) {
    if (msg == WM_KEYDOWN && activeKeys) {
        if (key == VK_RETURN) activeKeys->next = EVT_OK;
        else if (key == VK_DOWN || key == VK_RIGHT) activeKeys->next = EVT_DOWN;
        else if (key == VK_UP || key == VK_LEFT) activeKeys->next = EVT_UP;
        else if (key == VK_ESCAPE) activeKeys->next = EVT_BACK;
        else if (key == 'T') AppManager::launch(new TemperatureApp());
        return 0;
    }
    if (msg == WM_PAINT && activeScreen) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 128; info.bmiHeader.biHeight = -64;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        RECT r{}; GetClientRect(hwnd, &r);
        StretchDIBits(dc, 0, 0, r.right, r.bottom, 0, 0, 128, 64,
                      activeScreen->pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hwnd, &paint); return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, msg, key, param);
}

void check(bool value, const char* description) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
}

bool STM32_ReadSensorSnapshot(StmSensorSnapshot* out) {
    if (!sampleAvailable) return false;
    *out = sample; return true;
}

int main(int argc, char** argv) {
    ULONG_PTR token{}; Gdiplus::GdiplusStartupInput gdiplus;
    if (Gdiplus::GdiplusStartup(&token, &gdiplus, nullptr) != Gdiplus::Ok) return 1;
    Heap heap; NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(now, nullptr);
    Screen screen; Keys keys;
    {
        WindowManager wm(&screen, &keys); AppManager::init(&screen, &keys);
        auto* counter = new CounterApp(); AppManager::launch(counter);
        auto step = [&]() { AppManager::tick(); wm.update(); wm.drawFrame(); clockMs += 16; };
        sample.pressure.is_valid = true; sample.pressure.temperature = 24.5f;
        sample.pressure.timestamp = clockMs;
        if (argc > 1 && std::string(argv[1]) == "--headless") {
            const auto output = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::current_path();
            std::filesystem::create_directories(output);
            step(); check(counter->currentCount() == 0, "initial count");
            keys.next = EVT_OK; step(); check(counter->currentCount() == 1, "OK increments");
            check(screen.save(output / "counter.png"), "save real counter pixels");
            keys.next = EVT_DOWN; step(); keys.next = EVT_OK; step();
            check(counter->currentCount() == 0, "Reset callback");
            auto* temp = new TemperatureApp(); AppManager::launch(temp); step();
            check(std::string(temp->displayedValue()) == "24.5 C", "valid sample");
            check(screen.save(output / "temperature-demo.png"), "save simulated sensor pixels");
            sampleAvailable = false; step(); check(std::string(temp->displayedValue()) == "N/A", "missing snapshot");
            sampleAvailable = true; sample.pressure.is_valid = false; step();
            check(std::string(temp->displayedValue()) == "N/A", "invalid sample");
            sample.pressure.is_valid = true; sample.pressure.timestamp = clockMs - 5001; step();
            check(std::string(temp->displayedValue()) == "N/A", "stale sample");
            sample.pressure.timestamp = clockMs; sample.pressure.temperature = std::numeric_limits<float>::quiet_NaN(); step();
            check(std::string(temp->displayedValue()) == "N/A", "nonfinite sample");
            keys.next = EVT_BACK; step(); check(wm.getStackSize() == 1, "Back returns to counter");
            for (unsigned i = 0; i < 50; ++i) { AppManager::launch(new TemperatureApp()); step(); keys.next = EVT_BACK; step(); }
        } else {
            WNDCLASSW wc{}; wc.lpfnWndProc = windowProc; wc.hInstance = GetModuleHandle(nullptr);
            wc.lpszClassName = L"CrabitTutorialPreview"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassW(&wc); RECT r{0, 0, 768, 384}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
            screen.window = CreateWindowW(wc.lpszClassName, L"Crabit App tutorial - T: simulated temperature, Esc: back",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, r.right-r.left, r.bottom-r.top,
                nullptr, nullptr, wc.hInstance, nullptr);
            check(screen.window != nullptr, "create preview window");
            activeScreen = &screen; activeKeys = &keys; ShowWindow(screen.window, SW_SHOW);
            MSG msg{}; bool running = true;
            while (running) {
                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) { running = false; break; }
                    TranslateMessage(&msg); DispatchMessage(&msg);
                }
                sample.pressure.timestamp = clockMs; step(); Sleep(16);
            }
            activeScreen = nullptr; activeKeys = nullptr;
        }
        AppManager::shutdown();
    }
    check(heap.live == 0, "UI allocations released");
    std::printf("tutorial apps: PASS; tracked UI peak=%zu bytes; live=0; sensor input is simulated\n", heap.peak);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    Gdiplus::GdiplusShutdown(token); return 0;
}
