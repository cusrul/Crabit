#define MAIN_CPP
#include <SDL2/SDL.h>
#include <cstdlib>
#include <cstring>

// Tracy Profiler 接入
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>

// 重载 new/delete 以追踪内存泄漏
void* operator new(size_t size) {
    void* ptr = malloc(size);
    TracyAlloc(ptr, size);
    return ptr;
}

void operator delete(void* ptr) noexcept {
    TracyFree(ptr);
    free(ptr);
}

void* operator new[](size_t size) {
    void* ptr = malloc(size);
    TracyAlloc(ptr, size);
    return ptr;
}

void operator delete[](void* ptr) noexcept {
    TracyFree(ptr);
    free(ptr);
}
#endif

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/memory_pool.h" // 引入内存抽象

// --- 桌面版标准内存适配器 ---
class StdMemoryAdapter : public NanoUI::MemoryAdapter {
public:
    void* alloc(size_t size) override { return ::malloc(size); }
    void free(void* ptr) override { ::free(ptr); }
};
static StdMemoryAdapter g_std_allocator;

#ifdef TRACY_ENABLE
static void traceUiAllocation(void* ptr, size_t size, const char* tag) {
    TracyAllocN(ptr, size, tag);
}

static void traceUiRelease(void* ptr, const char* tag) {
    TracyFreeN(ptr, tag);
}
#endif

#include "NanoUI/core/data_hub.h"
#include "hal_mock.h"

// 硬件驱动层
#include "sdl_adapter.h"
#include "sdl_input.h"

// 引入桌面应用
#include "app_clock_new.h"
#include "launcher.h"

// 实例化 Mock 全局变量
GlobalDataStore GlobalStore;
SPI_HandleTypeDef hspi3;
UART_HandleTypeDef huart1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim15;

int main(int argc, char *argv[]) {
    const char* snapshotPath = argc == 3 && !std::strcmp(argv[1], "--snapshot") ? argv[2] : nullptr;
    
    // 注入适配器（需在创建 UI 对象前）
    NanoUI::MemoryPool::instance().setAdapter(&g_std_allocator);
#ifdef TRACY_ENABLE
    NanoUI::MemoryPool::instance().setObservers(traceUiAllocation, traceUiRelease);
#endif

    // 注入时间基准
    HAL_Time::init(
        []() -> uint32_t { return SDL_GetTicks(); },
        [](uint32_t ms) { SDL_Delay(ms); }
    );

    // 初始化 SDL 窗口
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return -1;
    SDL_Window *window = SDL_CreateWindow(
        "NANO-UI COMPONENT GALLERY", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        128 * 4, 64 * 4, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetScale(renderer, 4, 4);

    // 挂载引擎与调度器
    SDLAdapter canvas(renderer, 128, 64);
    canvas.setSnapshotPath(snapshotPath);
    SDLInputDriver input;
    input.setEventTracing(snapshotPath != nullptr);
    WindowManager wm(&canvas, &input);
    AppManager::init(&canvas, &input);

    // 启动主启动器
    AppManager::launch(new LauncherApp());

    bool running = true;
    uint32_t last_sim_tick = 0;

    while (running) {
        #ifdef TRACY_ENABLE
        ZoneScopedN("MainLoop"); // 在 Tracy 中标记主循环区域
        #endif

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            input.injectSDLEvent(e);
        }

        uint32_t now = SDL_GetTicks();
        if (now - last_sim_tick > 20) {
            last_sim_tick = now;
            
            // 模拟传感器数据流
            static float angle = 0; angle += 0.05f;
            DataHub::shared.optical.dx = (int8_t)(cos(angle) * 5);
            DataHub::shared.optical.dy = (int8_t)(sin(angle) * 5);
            DataHub::shared.battery.percent = (int)(99 - (now/5000)%10);
            DataHub::shared.uptimeMs = now;
        }

        // --- 核心调度 ---
        AppManager::tick();  
        wm.update();
        // wm.markDirty();       
        wm.drawFrame();      

        #ifdef TRACY_ENABLE
        FrameMark; // 在 Tracy 中标记一帧的结束
        #endif

        SDL_Delay(16);
    }

    AppManager::shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
