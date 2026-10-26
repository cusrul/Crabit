#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <memory>

namespace {
int failures = 0;
int sequence = 0;
int rootDestroyed = 0;
int childDestroyed = 0;
int backgroundDestroyed = 0;
int attemptsDestroyed = 0;
int borrowedDestroyed = 0;
int configDestroyed = 0;
int renderDestroyed = 0;
int builds = 0;
int resumes = 0;
bool pendingConfigAlive = false;
int pendingRenderOrder = 0;
int pendingConfigOrder = 0;
bool rawConfigAlive = false;
bool rawRenderSawConfigAlive = false;
int reuseBuilds = 0;
int reuseDestroyCallbacks = 0;
int reuseAppDestroyed = 0;
int shutdownProbeRenderDestroyed = 0;
int shutdownProbeWidgetDestroyed = 0;
int rejectedWidgetDestroyed = 0;
std::vector<int> order;
std::vector<int> frameLifecycle;
std::vector<int> appLifecycle;

#define CHECK(value)                                        \
    do {                                                    \
        if (!(value)) {                                     \
            ++failures;                                     \
            std::printf("line %d: %s\n", __LINE__, #value); \
        }                                                   \
    } while (false)

class CountingAdapter final : public NanoUI::MemoryAdapter {
   public:
    size_t live = 0;
    void* alloc(size_t size) override {
        void* p = std::malloc(size);
        if (p)
            ++live;
        return p;
    }
    void free(void* p) override {
        if (p) {
            --live;
            std::free(p);
        }
    }
};

class TestCanvas final : public Canvas {
   public:
    unsigned displays = 0;
    void clear() override {}
    void display() override { ++displays; }
    int16_t width() override { return 64; }
    int16_t height() override { return 32; }
    void drawPixel(int16_t, int16_t, uint16_t) override {}
};

class OrderedRender final : public RenderSizedBox {
   public:
    explicit OrderedRender(int& destroyed) : RenderSizedBox(8, 8), destroyed_(&destroyed) {}
    ~OrderedRender() override {
        ++*destroyed_;
        order.push_back(++sequence);
    }

   private:
    int* destroyed_;
};

class ConfigWidget final : public Widget {
   public:
    ~ConfigWidget() override {
        ++configDestroyed;
        order.push_back(++sequence);
    }
    RenderObject* createRenderObject() override { return new OrderedRender(renderDestroyed); }
};

class RawRouteRender final : public RenderSizedBox {
   public:
    explicit RawRouteRender(bool* configAlive) : RenderSizedBox(8, 8), configAlive_(configAlive) {}
    ~RawRouteRender() override { rawRenderSawConfigAlive = *configAlive_; }

   private:
    bool* configAlive_;
};

class RawRouteWidget final : public Widget {
   public:
    RawRouteWidget() { rawConfigAlive = true; }
    ~RawRouteWidget() override { rawConfigAlive = false; }
    RenderObject* createRenderObject() override { return new RawRouteRender(&rawConfigAlive); }
};

class PendingRender final : public RenderSizedBox {
   public:
    explicit PendingRender(bool* alive) : RenderSizedBox(8, 8), alive_(alive) {}
    ~PendingRender() override {
        CHECK(*alive_);
        pendingRenderOrder = ++sequence;
    }

   private:
    bool* alive_;
};

class PendingPopup final : public Widget {
   public:
    PendingPopup() { pendingConfigAlive = true; }
    ~PendingPopup() override {
        pendingConfigAlive = false;
        pendingConfigOrder = ++sequence;
    }
    RenderObject* createRenderObject() override { return new PendingRender(&pendingConfigAlive); }
};

class ReuseApp final : public Application {
   public:
    const char* getAppName() override { return "reuse-after-shutdown"; }
    Widget* buildUI() override {
        ++reuseBuilds;
        return new ConfigWidget();
    }
    void onDestroy() override { ++reuseDestroyCallbacks; }
    ~ReuseApp() override { ++reuseAppDestroyed; }
};

class RejectedWidget final : public Widget {
   public:
    ~RejectedWidget() override { ++rejectedWidgetDestroyed; }
    RenderObject* createRenderObject() override { return new RenderSizedBox(1, 1); }
};

class ShutdownProbeRender final : public RenderSizedBox {
   public:
    ShutdownProbeRender(WindowManager* manager, bool recurse)
        : RenderSizedBox(8, 8), manager_(manager), recurse_(recurse) {}
    ~ShutdownProbeRender() override {
        ++shutdownProbeRenderDestroyed;
        if (recurse_) {
            manager_->clear();                     // recursive clear must be a no-op
            manager_->push(new RejectedWidget());  // terminal push must consume ownership
        }
    }

   private:
    WindowManager* manager_;
    bool recurse_;
};

class ShutdownProbeWidget final : public Widget {
   public:
    ShutdownProbeWidget(WindowManager* manager, bool recursiveRender)
        : manager_(manager), recursiveRender_(recursiveRender) {}
    ~ShutdownProbeWidget() override {
        ++shutdownProbeWidgetDestroyed;
        manager_->push(new RejectedWidget());
        RejectedWidget borrowedPopup;
        Popup::show(&borrowedPopup, 4, 4);
    }
    RenderObject* createRenderObject() override {
        return new ShutdownProbeRender(manager_, recursiveRender_);
    }

   private:
    WindowManager* manager_;
    bool recursiveRender_;
};

class ShutdownApp : public Application {
   public:
    ShutdownApp(const char* name, int& destroyed, AppMode mode = APP_MODE_COOPERATIVE, int id = 0)
        : name_(name), destroyed_(&destroyed), mode_(mode), id_(id) {}
    const char* getAppName() override { return name_; }
    AppMode getMode() override { return mode_; }
    Widget* buildUI() override {
        ++builds;
        return new ConfigWidget();
    }
    void onResume() override { ++resumes; }
    void onCreate() override { ++created; }
    void onDestroy() override {
        ++destroyedCallback;
        appLifecycle.push_back(100 + id_);
    }
    ~ShutdownApp() override {
        ++*destroyed_;
        ++destroyedObject;
        appLifecycle.push_back(200 + id_);
    }
    static int created;
    static int destroyedCallback;
    static int destroyedObject;

   private:
    const char* name_;
    int* destroyed_;
    AppMode mode_;
    int id_;
};
int ShutdownApp::created = 0;
int ShutdownApp::destroyedCallback = 0;
int ShutdownApp::destroyedObject = 0;

class ReentrantApp final : public ShutdownApp {
   public:
    ReentrantApp() : ShutdownApp("reentrant", attemptsDestroyed) {}
    void onDestroy() override {
        ShutdownApp::onDestroy();
        AppManager::launch(new ShutdownApp("late-launch", attemptsDestroyed));
        AppManager::launch(
            new ShutdownApp("late-background", attemptsDestroyed, APP_MODE_BACKGROUND));
        Navigator::push(new ConfigWidget());
        ConfigWidget borrowedPopup;
        Popup::show(&borrowedPopup, 8, 8);
        AppManager::shutdown();  // terminal cleanup must not recurse from a lifecycle callback
    }
};

class OrderedFrame final : public ShutdownApp {
   public:
    OrderedFrame(const char* name, int& destroyed, int id)
        : ShutdownApp(name, destroyed, APP_MODE_FRAME, id), id_(id) {}
    ~OrderedFrame() override { frameLifecycle.push_back(id_ * 10 + 3); }
    void onUnmount() override {
        unmounted = true;
        order.push_back(++sequence);
        frameLifecycle.push_back(id_ * 10 + 1);
    }
    void onDestroy() override {
        CHECK(unmounted);
        order.push_back(++sequence);
        frameLifecycle.push_back(id_ * 10 + 2);
        ShutdownApp::onDestroy();
    }
    bool unmounted = false;

   private:
    int id_;
};
}  // namespace

int main() {
    CountingAdapter adapter;
    NanoUI::MemoryPool::instance().setAdapter(&adapter);
    TestCanvas canvas;

    // WindowManager owns render trees and route-owned configuration, but not
    // the borrowed Application. Its destructor must perform the same drain.
    {
        WindowManager manager(&canvas, nullptr);
        ShutdownApp borrowed("borrowed", borrowedDestroyed);
        manager.push(new ConfigWidget(), &borrowed);
        CHECK(configDestroyed == 1);  // owner-backed config is released after render creation
        manager.push(new RawRouteWidget());
        CHECK(manager.getStackSize() == 2);
    }
    CHECK(renderDestroyed ==
          1);  // the owner-backed config's render; raw fixture has its own liveness probe
    CHECK(configDestroyed == 1);
    CHECK(!rawConfigAlive &&
          rawRenderSawConfigAlive);  // raw Render dies before its Widget configuration
    CHECK(attemptsDestroyed == 0);

    // Keep a live WindowManager so cooperative Apps install real Render trees.
    // Exercise both a materialized Popup and Toast before terminal shutdown.
    {
        WindowManager lifecycleManager(&canvas, nullptr);
        AppManager::init(&canvas, nullptr);
        AppManager::launch(new ShutdownApp("root", rootDestroyed, APP_MODE_COOPERATIVE, 3));
        AppManager::launch(new ShutdownApp("child", childDestroyed, APP_MODE_COOPERATIVE, 4));
        AppManager::launch(new OrderedFrame("frame-root", rootDestroyed, 1));
        AppManager::launch(new OrderedFrame("frame-top", childDestroyed, 2));
        AppManager::launch(
            new ShutdownApp("background", backgroundDestroyed, APP_MODE_BACKGROUND, 5));
        std::unique_ptr<Widget> popupConfig(new PendingPopup());
        Popup::show(popupConfig.get(), 8, 8);
        Popup::showToast("shutdown", 60000);
        lifecycleManager.drawFrame();  // forces Popup RenderObject creation
        CHECK(pendingConfigAlive);
        CHECK(Popup::isVisible && Popup::pendingContent != nullptr);
        CHECK(Popup::toastMsg != nullptr && Popup::toastEndTime != 0);
        const int buildsBeforeShutdown = builds;
        const int resumesBeforeShutdown = resumes;
        AppManager::shutdown();
        CHECK(rootDestroyed == 2 && childDestroyed == 2 && backgroundDestroyed == 1);
        CHECK(ShutdownApp::created == ShutdownApp::destroyedCallback);
        CHECK(ShutdownApp::created + 1 ==
              ShutdownApp::destroyedObject);  // plus stack-local borrowed WM owner
        CHECK(frameLifecycle == std::vector<int>({21, 22, 23, 11, 12, 13}));
        CHECK(appLifecycle ==
              std::vector<int>({200, 102, 202, 101, 201, 104, 204, 103, 203, 105, 205}));
        CHECK(builds == buildsBeforeShutdown && resumes == resumesBeforeShutdown);
        CHECK(!AppManager::frameAppActive());
        CHECK(pendingConfigAlive && pendingRenderOrder > 0);
        popupConfig.reset();  // Caller ownership survives framework shutdown.
        CHECK(!pendingConfigAlive && pendingRenderOrder < pendingConfigOrder);
        CHECK(Popup::pendingContent == nullptr && !Popup::isVisible);
        CHECK(Popup::targetW == 0 && Popup::targetH == 0);
        CHECK(Popup::toastMsg == nullptr && Popup::toastEndTime == 0);
        CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
        CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
        CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
        CHECK(adapter.live == 0);

        // A terminal drain is repeatable, and this same live WM can host a
        // fresh cooperative UI afterward; prove build, route install and draw.
        AppManager::shutdown();
        CHECK(WindowManager::instance == &lifecycleManager);
        AppManager::init(&canvas, nullptr);
        AppManager::launch(new ReuseApp());
        CHECK(lifecycleManager.getStackSize() == 1);
        CHECK(reuseBuilds == 1 && reuseDestroyCallbacks == 0 && reuseAppDestroyed == 0);
        const unsigned displaysBeforeReuseDraw = canvas.displays;
        lifecycleManager.drawFrame();
        CHECK(canvas.displays > displaysBeforeReuseDraw);
        AppManager::shutdown();
        CHECK(reuseDestroyCallbacks == 1 && reuseAppDestroyed == 1);
        CHECK(lifecycleManager.getStackSize() == 0);
        CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
        CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
        CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
        CHECK(adapter.live == 0);
    }
    CHECK(WindowManager::instance == nullptr);  // independently verify WM RAII detach

    AppManager::shutdown();
    AppManager::shutdown();
    AppManager::init(&canvas, nullptr);
    AppManager::launch(new ShutdownApp("reinit", rootDestroyed));
    AppManager::shutdown();
    CHECK(rootDestroyed == 3);
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
    CHECK(adapter.live == 0);

    {
        WindowManager callbackManager(&canvas, nullptr);
        AppManager::init(&canvas, nullptr);
        AppManager::launch(new ReentrantApp());
        const int createdBeforeShutdown = ShutdownApp::created;
        AppManager::shutdown();
        CHECK(attemptsDestroyed == 3);  // reentrant App plus both rejected launch objects
        CHECK(ShutdownApp::created == createdBeforeShutdown);
        CHECK(Popup::pendingContent == nullptr && !Popup::isVisible);
        CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
        CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
        CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
        CHECK(adapter.live == 0);
    }

    {
        WindowManager reentrantManager(&canvas, nullptr);
        reentrantManager.push(new PopupRoot(new ShutdownProbeWidget(&reentrantManager, true)));
        std::unique_ptr<Widget> popupConfig(new ShutdownProbeWidget(&reentrantManager, false));
        Popup::show(popupConfig.get(), 8, 8);
        reentrantManager.drawFrame();  // install the pending Popup Render tree
        CHECK(Popup::pendingContent != nullptr);
        AppManager::init(&canvas, nullptr);
        const int appCreationsBeforeDestructionCallbacks = ShutdownApp::created;
        AppManager::shutdown();
        CHECK(ShutdownApp::created == appCreationsBeforeDestructionCallbacks);
        {
            NanoUI::detail::ShutdownGuard guard;
            popupConfig.reset();  // App owner cleans up only after render teardown.
        }
        CHECK(shutdownProbeRenderDestroyed == 2);  // root + popup render, exactly once each
        CHECK(shutdownProbeWidgetDestroyed == 2);  // route config + pending popup config
        CHECK(rejectedWidgetDestroyed == 5);       // Render push; each Widget destructor push/show
        CHECK(reentrantManager.getStackSize() == 0);
        CHECK(Popup::pendingContent == nullptr && !Popup::isVisible);
        CHECK(Popup::targetW == 0 && Popup::targetH == 0);
        CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
        CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
        CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
        CHECK(adapter.live == 0);
    }

    // Keep the adapter alive until static-manager teardown has completed.
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("shutdown failures=%d\n", failures);
    return failures ? 1 : 0;
}
