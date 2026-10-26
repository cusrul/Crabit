#ifndef FRAMEWORK_APP_H
#define FRAMEWORK_APP_H
#include <stdint.h>
#include "widget.h"
#include "../hal/canvas.h"
#include "../hal/input.h"

// 定义 App 的运行模式
enum AppMode {
    APP_MODE_COOPERATIVE,  // 前台协同：有 UI，跟着 OS 轮询
    APP_MODE_EXCLUSIVE,    // 前台独占：有 UI，霸占整个单片机
    APP_MODE_BACKGROUND,   // 纯后台服务：无 UI，默默在后台轮询
    APP_MODE_FRAME         // 非阻塞帧应用：每次只运行一个生命周期步骤
};

struct UiEvent {
    uint8_t type = 0;
    int16_t x = 0;
    int16_t y = 0;
    int16_t value = 0;
};

class Application {
   private:
    bool exit_requested = false;

   public:
    virtual ~Application() = default;

    virtual const char* getAppName() = 0;

    // 【核心新增】默认是协同模式。独占 App 需要重写它返回 APP_MODE_EXCLUSIVE
    virtual AppMode getMode() { return APP_MODE_COOPERATIVE; }

    virtual void onCreate() {}
    virtual void onDestroy() {}

    // 帧应用接口。实现不得阻塞；每个回调都必须在一次调用内返回。
    virtual void onMount(Canvas* canvas, InputDriver* input) {
        (void)canvas;
        (void)input;
    }
    virtual void onEvent(const UiEvent& event) { (void)event; }
    virtual void onUpdate(uint32_t now_ms) { (void)now_ms; }
    virtual void onRender(Canvas* canvas) { (void)canvas; }
    virtual void onSuspend() {}
    virtual void onResume() {}
    virtual void onUnmount() {}

    void requestExit() { exit_requested = true; }
    bool exitRequested() const { return exit_requested; }

    // ==========================================
    // 接口 A：协同模式 (Cooperative) 专用
    // ==========================================
    virtual void onTick() {}
    virtual Widget* buildUI() { return nullptr; }

    // ==========================================
    // 接口 B：独占模式 (Exclusive) 专用
    // ==========================================
    // OS 会把屏幕画笔 (Canvas) 和按键驱动 (Input) 直接交给你
    virtual void runExclusive(Canvas* canvas, InputDriver* input) {}
};

template <typename T>
class ApplicationWrapper : public Application {
   private:
    const char* name;

   public:
    ApplicationWrapper(const char* n) : name(n) {}
    const char* getAppName() override { return name; }
    Widget* buildUI() override { return new T(); }
};

#endif
