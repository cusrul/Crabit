#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <NanoUI.h>
#include "apps/hello_app.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
const int kWidth = 128;
const int kHeight = 64;
const int kScale = 4;

class WindowsCanvas : public Canvas {
public:
    WindowsCanvas() : pixels(kWidth * kHeight, 0) { resetClipRect(); }
    void clear() override { std::fill(pixels.begin(), pixels.end(), 0); }
    void display() override;
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
            unsigned char& pixel = pixels[static_cast<size_t>(y) * kWidth + x];
            if (color == C_INVERT) pixel = pixel ? 0 : 1;
            else pixel = color == C_BLACK ? 0 : 1;
        }
    }
    int16_t width() override { return kWidth; }
    int16_t height() override { return kHeight; }
    bool saveBmp(const char* path) const {
        FILE* file = std::fopen(path, "wb");
        if (!file) return false;
        const uint32_t pixelBytes = kWidth * kHeight * 3;
        unsigned char header[54] = {};
        header[0] = 'B'; header[1] = 'M';
        put32(header + 2, 54 + pixelBytes); put32(header + 10, 54);
        put32(header + 14, 40); put32(header + 18, kWidth); put32(header + 22, kHeight);
        put16(header + 26, 1); put16(header + 28, 24); put32(header + 34, pixelBytes);
        bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header);
        for (int y = kHeight - 1; ok && y >= 0; --y) {
            for (int x = 0; x < kWidth; ++x) {
                const unsigned char level = pixels[static_cast<size_t>(y) * kWidth + x] ? 255 : 0;
                const unsigned char bgr[3] = {level, level, level};
                if (std::fwrite(bgr, 1, 3, file) != 3) { ok = false; break; }
            }
        }
        if (std::fclose(file) != 0) ok = false;
        return ok;
    }
    const std::vector<unsigned char>& image() const { return pixels; }
private:
    std::vector<unsigned char> pixels;
    static void put16(unsigned char* p, uint16_t v) { p[0] = v & 255; p[1] = v >> 8; }
    static void put32(unsigned char* p, uint32_t v) {
        p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = v >> 24;
    }
};

WindowsCanvas* activeCanvas = nullptr;
HWND windowHandle = nullptr;
uint32_t ticks() { return GetTickCount(); }
void delayMs(uint32_t ms) { Sleep(ms); }

class WindowsInput : public InputDriver {
public:
    WindowsInput() : pending(EVT_NONE) {}
    void init() override {}
    UIEvent poll() override { UIEvent event = {pending, 0}; pending = EVT_NONE; return event; }
    void queue(EventType event) { pending = event; }
private:
    EventType pending;
};

WindowsInput* activeInput = nullptr;
void WindowsCanvas::display() {
    if (!windowHandle) return;
    InvalidateRect(windowHandle, nullptr, FALSE);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client; GetClientRect(hwnd, &client);
        HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        FillRect(dc, &client, black);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = kWidth; info.bmiHeader.biHeight = -kHeight;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        std::vector<uint32_t> rgb(kWidth * kHeight);
        for (size_t i = 0; i < rgb.size(); ++i) {
            const uint32_t v = activeCanvas->image()[i] ? 0x00FFFFFFu : 0;
            rgb[i] = v;
        }
        StretchDIBits(dc, 0, 0, kWidth * kScale, kHeight * kScale, 0, 0, kWidth, kHeight,
                      rgb.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hwnd, &paint); return 0;
    }
    if (message == WM_KEYDOWN && activeInput) {
        switch (wParam) {
        case VK_UP: activeInput->queue(EVT_UP); break;
        case VK_DOWN: activeInput->queue(EVT_DOWN); break;
        case VK_LEFT: activeInput->queue(EVT_LEFT); break;
        case VK_RIGHT: activeInput->queue(EVT_RIGHT); break;
        case VK_RETURN: activeInput->queue(EVT_OK); break;
        case VK_ESCAPE: activeInput->queue(EVT_BACK); break;
        }
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

int runHeadless(int argc, char** argv) {
    const char* output = argc > 2 ? argv[2] : "FirstApp.bmp";
    WindowsCanvas canvas; WindowsInput input; HelloApp app;
    HAL_Time::init(ticks, delayMs); input.init();
    WindowManager manager(&canvas, &input);
    manager.push(app.buildUI(), &app, [](Application* owner) { return owner->buildUI(); });
    manager.drawFrame();
    input.queue(EVT_OK); manager.update(); manager.drawFrame();
    const bool saved = canvas.saveBmp(output);
    std::printf("switch=%s bmp=%s\n", app.isEnabled() ? "on" : "off", output);
    return saved && app.isEnabled() ? 0 : 1;
}
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--headless") == 0) return runHeadless(argc, argv);

    WNDCLASSA wc = {};
    wc.lpfnWndProc = windowProc; wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "NanoUIFirstAppWindow"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassA(&wc)) return 1;
    RECT bounds = {0, 0, kWidth * kScale, kHeight * kScale};
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    windowHandle = CreateWindowA(wc.lpszClassName, "NanoUI FirstApp", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!windowHandle) return 1;

    WindowsCanvas canvas; WindowsInput input; HelloApp app;
    activeCanvas = &canvas; activeInput = &input;
    HAL_Time::init(ticks, delayMs); input.init();
    WindowManager manager(&canvas, &input);
    manager.push(app.buildUI(), &app, [](Application* owner) { return owner->buildUI(); });
    ShowWindow(windowHandle, SW_SHOW); manager.drawFrame();
    MSG message;
    while (true) {
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return 0;
            TranslateMessage(&message); DispatchMessage(&message);
        }
        manager.update(); manager.drawFrame(); Sleep(16);
    }
}
