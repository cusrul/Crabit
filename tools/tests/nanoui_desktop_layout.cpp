// Host-only observation harness: renders the existing desktop Apps unchanged.
// It uses the real WindowManager, resources and focus/scroll event path.
#include <NanoUI.h>
#include "NanoUI/core/data_hub.h"
#include "hal_mock.h"
#include "app_clock_new.h"
#ifdef NANOUI_DESKTOP_SDL_INPUT
#include "sdl_input.h"
#include "sdl_adapter.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

GlobalDataStore GlobalStore;
SPI_HandleTypeDef hspi3;
UART_HandleTypeDef huart1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim15;

class ObservationHeap : public NanoUI::MemoryAdapter {
public:
    void* alloc(size_t n) override { return std::malloc(n); }
    void free(void* p) override { std::free(p); }
};

class ObservationCanvas : public Canvas {
public:
    uint8_t pixels[128 * 64]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        uint8_t& p = pixels[y * 128 + x];
        if (color == C_INVERT) p ^= 1;
        else p = color == C_WHITE;
    }
    bool save(const char* path) {
        const unsigned w = 512, h = 256, bytes = 54 + w * h * 3;
        uint8_t header[54]{};
        header[0] = 'B'; header[1] = 'M'; header[10] = 54; header[14] = 40;
        for (int i = 0; i < 4; ++i) {
            header[2 + i] = (bytes >> (8 * i)) & 255;
            header[18 + i] = (w >> (8 * i)) & 255;
            header[22 + i] = (h >> (8 * i)) & 255;
        }
        header[26] = 1; header[28] = 24;
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        bool ok = std::fwrite(header, 1, sizeof header, f) == sizeof header;
        for (int y = h - 1; y >= 0; --y) for (unsigned x = 0; x < w; ++x) {
            const uint8_t p = pixels[(y / 4) * 128 + x / 4] ? 255 : 0;
            const uint8_t rgb[] = {p, p, p};
            ok = (std::fwrite(rgb, 1, 3, f) == 3) && ok;
        }
        return std::fclose(f) == 0 && ok;
    }
};

static unsigned ticks;
static unsigned getTicks() { return ticks; }

int main(int argc, char** argv) {
    if (argc < 3) {
        std::puts("usage: NanoUI_DesktopLayoutCheck launcher|sensors|system|games|tools|fonts|nested|clock output.bmp [down-count]");
        return 2;
    }
    ObservationHeap heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(getTicks, nullptr);
    ObservationCanvas canvas;
    bool sdlInputMode = false;
    bool sdlPresentationMode = false;
    Canvas* activeCanvas = &canvas;
#ifdef NANOUI_DESKTOP_SDL_INPUT
    sdlPresentationMode = !std::strcmp(argv[1], "sdl-render-sensors") || !std::strcmp(argv[1], "sdl-render-back");
    sdlInputMode = sdlPresentationMode || !std::strcmp(argv[1], "sdl-input-sensors") || !std::strcmp(argv[1], "sdl-input-back");
    if (sdlInputMode && SDL_Init(SDL_INIT_EVENTS) != 0) return 3;
    SDL_Surface* surface = sdlPresentationMode
        ? SDL_CreateRGBSurfaceWithFormat(0, 128, 64, 32, SDL_PIXELFORMAT_ARGB8888) : nullptr;
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (sdlPresentationMode && !renderer) return 3;
    SDLAdapter presentation(renderer, 128, 64);
    if (sdlPresentationMode) activeCanvas = &presentation;
    SDLInputDriver input;
    InputDriver* activeInput = sdlInputMode ? &input : nullptr;
    WindowManager wm(activeCanvas, activeInput);
#else
    InputDriver* activeInput = nullptr;
    WindowManager wm(&canvas, nullptr);
#endif
    Application* app = nullptr;
    const bool navigation = sdlInputMode || !std::strcmp(argv[1], "navigate-sensors") || !std::strcmp(argv[1], "navigate-back");
    if (!std::strcmp(argv[1], "launcher") || navigation) app = new LauncherApp;
    else if (!std::strcmp(argv[1], "sensors")) app = new FolderSensorsApp;
    else if (!std::strcmp(argv[1], "system")) app = new FolderSystemApp;
    else if (!std::strcmp(argv[1], "games")) app = new FolderGamesApp;
    else if (!std::strcmp(argv[1], "tools")) app = new FolderToolsApp;
    else if (!std::strcmp(argv[1], "fonts")) app = new FontGalleryApp;
    else if (!std::strcmp(argv[1], "nested")) app = new ComplexTestApp;
    else if (!std::strcmp(argv[1], "clock")) app = new CyberClockApp;
    else return 2;
    if (navigation) { AppManager::init(activeCanvas, activeInput); AppManager::launch(app); }
    else { app->onCreate(); wm.push(app->buildUI(), app); }
    // Direct scenes borrow an App; navigation scenes transfer it to AppManager.
    // Both must release UI objects before their local allocator/SDL adapter.
    struct Cleanup {
        Application* app;
        bool managed;
        void close() {
            if (!app) return;
            Application* owner = app;
            app = nullptr;
            AppManager::shutdown();
            if (!managed) { owner->onDestroy(); delete owner; }
        }
        ~Cleanup() { close(); }
    } cleanup{app, navigation};
    auto settle = [&]() {
        for (int frame = 0; frame < 120; ++frame) {
            ticks += 16; wm.drawFrame();
        }
    };
    settle();
    auto send = [&](EventType event) {
#ifdef NANOUI_DESKTOP_SDL_INPUT
        if (sdlInputMode) {
            SDL_Event key{};
            key.type = SDL_KEYDOWN;
            key.key.keysym.sym = event == EVT_OK ? SDLK_RETURN : event == EVT_BACK ? SDLK_ESCAPE : SDLK_DOWN;
            if (SDL_PushEvent(&key) != 1) return false;
            wm.update();
            return true;
        }
#endif
        wm.dispatchEvent({event, 0});
        return true;
    };
#ifdef NANOUI_DESKTOP_SDL_INPUT
    if (sdlInputMode) {
        // IME/text composition is not a keyboard-down event. It must not open
        // a page, and it must not be misreported as a successful Return test.
        SDL_Event composition{}; composition.type = SDL_TEXTEDITING;
        if (SDL_PushEvent(&composition) != 1) return 3;
        wm.update(); settle();
        if (wm.getStackSize() != 1) return 4;
    }
#endif
    if (navigation) {
        if (!send(EVT_OK)) return 3;
        settle();
        if (wm.getStackSize() != 2) return 4;
        if (!std::strcmp(argv[1], "navigate-back") || !std::strcmp(argv[1], "sdl-input-back") || !std::strcmp(argv[1], "sdl-render-back")) {
            if (!send(EVT_BACK)) return 3;
            settle();
            if (wm.getStackSize() != 1) return 4;
        }
    }
    const int count = argc > 3 ? std::atoi(argv[3]) : 0;
    for (int step = 0; step < count; ++step) { if (!send(EVT_DOWN)) return 3; settle(); }
#ifdef NANOUI_DESKTOP_SDL_INPUT
    if (sdlPresentationMode) {
        // Read the real SDL software renderer's surface, not an alternate UI
        // render. This verifies the desktop Canvas adapter without OS input or
        // a visible window. It does not establish GPU/window-compositor health.
        if (SDL_LockSurface(surface) != 0) return 3;
        for (int y = 0; y < 64; ++y) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(
                static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch);
            for (int x = 0; x < 128; ++x) {
                uint8_t r, g, b;
                SDL_GetRGB(row[x], surface->format, &r, &g, &b);
                if (!((r == 0 || r == 255) && r == g && g == b)) return 4;
                canvas.pixels[y * 128 + x] = r != 0;
            }
        }
        SDL_UnlockSurface(surface);
    }
#endif
    unsigned ink = 0;
    for (auto p : canvas.pixels) ink += p != 0;
    std::printf("page=%s down=%d ink=%u focus=%s pages=%u sdl-input=%d software-presentation=%d\n", argv[1], count, ink,
        wm.getFocusedRenderObject() ? "present" : "none", unsigned(wm.getStackSize()), sdlInputMode, sdlPresentationMode);
    const int result = ink && canvas.save(argv[2]) ? 0 : 1;
    cleanup.close();
#ifdef NANOUI_DESKTOP_SDL_INPUT
    if (renderer) SDL_DestroyRenderer(renderer);
    if (surface) SDL_FreeSurface(surface);
    if (sdlInputMode) SDL_Quit();
#endif
    return result;
}
