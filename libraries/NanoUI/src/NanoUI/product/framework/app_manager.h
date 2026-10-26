#ifndef CORE_APP_MANAGER_H
#define CORE_APP_MANAGER_H

#include "../framework/app.h"
#include "../core/window_manager.h"
#include "../widgets/popup.h"
#include "../core/embedded_vector.h"

class AppManager {
private:
    // 前台应用按栈保留，退出顶层应用后恢复下层应用。
    static NanoUI::EmbeddedVector<Application*> appStack;
    static NanoUI::EmbeddedVector<Application*> backgroundApps;
    static Canvas* sysCanvas;
    static InputDriver* sysInput;

    static Widget* rebuildPage(Application* app) {
        Widget* ui = app->buildUI();
        return ui ? new PopupRoot(ui) : nullptr;
    }

public:

    static void init(Canvas* c, InputDriver* i) {
        if (NanoUI::detail::shutdownInProgress()) return;
        sysCanvas = c;
        sysInput = i;

        if (WindowManager::instance) {
            WindowManager::instance->onSystemBackPressed = []() {
                exitCurrentApp(); // 钩子统一调配，安全退出
            };
        }
    }

    static void launch(Application* app) {
        if (!app) return;
        if (NanoUI::detail::shutdownInProgress()) { delete app; return; }
        // [后台 App]
        if (app->getMode() == APP_MODE_BACKGROUND) {
            app->onCreate();
            backgroundApps.push_back(app);
            return;
        }

        // 下层应用仍由应用栈持有；压入新应用时不销毁它。
        if (!appStack.empty() && appStack.back()->getMode() == APP_MODE_FRAME) {
            appStack.back()->onSuspend();
        }
        app->onCreate();
        appStack.push_back(app);

        if (app->getMode() == APP_MODE_FRAME) {
            app->onMount(sysCanvas, sysInput);
        } else if (app->getMode() == APP_MODE_COOPERATIVE) {
            if (WindowManager::instance) {
                Widget* ui = app->buildUI();
                if (ui) WindowManager::instance->push(new PopupRoot(ui), app, rebuildPage);
            }
        } else if (app->getMode() == APP_MODE_EXCLUSIVE) {
            // Compatibility only. New applications must use APP_MODE_FRAME.
            app->runExclusive(sysCanvas, sysInput);
            exitCurrentApp();
        }
    }

    static void exitCurrentApp() {
        if (NanoUI::detail::shutdownInProgress()) return;
        if (appStack.empty()) return;

        // 普通返回不能退出根应用；整体销毁由 shutdown() 处理。
        if (appStack.size() <= 1) {
            return;
        }

        // 安全取出顶层 App
        Application* topApp = appStack.back();
        AppMode mode = topApp->getMode();

        // 销毁顶层 App 并出栈
        if (mode == APP_MODE_FRAME) topApp->onUnmount();
        topApp->onDestroy();
        if (mode == APP_MODE_COOPERATIVE && WindowManager::instance) {
            WindowManager::instance->pop();
        }
        delete topApp;
        appStack.pop_back();

        if (!appStack.empty() && appStack.back()->getMode() == APP_MODE_FRAME) {
            appStack.back()->onResume();
        }
    }

    static void dispatchEvent(const UiEvent& event) {
        if (NanoUI::detail::shutdownInProgress()) return;
        if (!appStack.empty() && appStack.back()->getMode() == APP_MODE_FRAME) {
            appStack.back()->onEvent(event);
        }
    }

    static bool frameAppActive() {
        if (NanoUI::detail::shutdownInProgress()) return false;
        return !appStack.empty() && appStack.back()->getMode() == APP_MODE_FRAME;
    }

    // Call from the port after its UI loop stops, before destroying Canvas,
    // InputDriver or MemoryAdapter. This is not a watch power/sleep operation.
    static void shutdown() {
        if (NanoUI::detail::shutdownInProgress()) return;
        NanoUI::detail::ShutdownGuard guard;
        if (WindowManager::instance) WindowManager::instance->clear();
        Popup::reset();
        while (!appStack.empty()) {
            Application* app = appStack.back();
            appStack.pop_back();
            if (app->getMode() == APP_MODE_FRAME) app->onUnmount();
            app->onDestroy();
            delete app;
        }
        while (!backgroundApps.empty()) {
            Application* app = backgroundApps.back();
            backgroundApps.pop_back();
            app->onDestroy();
            delete app;
        }
        appStack = decltype(appStack){};
        backgroundApps = decltype(backgroundApps){};
        sysCanvas = nullptr;
        sysInput = nullptr;
    }

    static void tick() {
        if (NanoUI::detail::shutdownInProgress()) return;
        // 只驱动最顶层（当前可见）的活跃 App
        if (!appStack.empty()) {
            Application* topApp = appStack.back();
            if (topApp->getMode() == APP_MODE_FRAME) {
                const uint32_t now = hal_get_ticks();
                topApp->onUpdate(now);
                topApp->onRender(sysCanvas);
                if (topApp == appStack.back() && topApp->exitRequested()) {
                    exitCurrentApp();
                }
            } else if (topApp->getMode() == APP_MODE_COOPERATIVE) {
                topApp->onTick();
            }
        }

        // 驱动所有后台
        for (auto bgApp : backgroundApps) {
            bgApp->onTick();
        }
    }
};

#endif
