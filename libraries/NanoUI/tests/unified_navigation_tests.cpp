// Unified original-library navigation contract. Intentionally include only
// the public umbrella: this test must not depend on either firmware tree.
// Simulate incoming SDK macros without depending on an actual SDK.
#define abs(value) NANOUI_INCOMING_ABS(value)
#define min(a, b) NANOUI_INCOMING_MIN(a, b)
#define max(a, b) NANOUI_INCOMING_MAX(a, b)
#define vsnprintf NANOUI_INCOMING_VSNPRINTF
#include <NanoUI.h>
#if !defined(abs) || !defined(min) || !defined(max) || !defined(vsnprintf)
#error "NanoUI must restore consumer macros after parsing its declarations"
#endif
#undef abs
#undef min
#undef max
#undef vsnprintf

#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
int activated = 0;
int ownerBuilds = 0;
int ownerWidgetsDestroyed = 0;
int ownerButtonsDestroyed = 0;
int rawRootWidgetsDestroyed = 0;
int rawRootButtonsDestroyed = 0;
int rawNestedWidgetsDestroyed = 0;
int rawNestedButtonsDestroyed = 0;

#define CHECK(value)                                        \
    do {                                                    \
        if (!(value)) {                                     \
            ++failures;                                     \
            std::printf("line %d: %s\n", __LINE__, #value); \
        }                                                   \
    } while (false)

class MallocAdapter final : public NanoUI::MemoryAdapter {
   public:
    void* alloc(size_t size) override { return std::malloc(size); }
    void free(void* ptr) override { std::free(ptr); }
};

class CountingButton final : public Button {
    int* destroyed_;

   public:
    CountingButton(VoidCallback callback, int& destroyed)
        : Button(new SizedBox(20, 10), callback), destroyed_(&destroyed) {}
    ~CountingButton() override { ++*destroyed_; }
};

class CountingRow final : public Row {
    int* destroyed_;

   public:
    CountingRow(int& rowDestroyed, int& buttonsDestroyed)
        : Row({new CountingButton(first, buttonsDestroyed),
               new CountingButton(second, buttonsDestroyed)}),
          destroyed_(&rowDestroyed) {}
    ~CountingRow() override { ++*destroyed_; }
    static void first() { activated = 1; }
    static void second() { activated = 2; }
};

class RootApplication final : public Application {
   public:
    const char* getAppName() override { return "unified-navigation-probe"; }
    Widget* buildUI() override {
        ++ownerBuilds;
        return new CountingRow(ownerWidgetsDestroyed, ownerButtonsDestroyed);
    }
};

Widget* rebuildRoot(Application* app) {
    return app->buildUI();
}

int appRootBuilds = 0;
bool childDestroyed = false;
bool childDestroyNotified = false;
bool childRenderDestroyed = false;

class AppRoot final : public Application {
   public:
    const char* getAppName() override { return "root"; }
    Widget* buildUI() override {
        ++appRootBuilds;
        if (appRootBuilds > 1)
            CHECK(childDestroyNotified && !childDestroyed);
        return new Button(new SizedBox(20, 10), CountingRow::first);
    }
};

class ChildRender final : public RenderSizedBox {
   public:
    ChildRender() : RenderSizedBox(20, 10) {}
    ~ChildRender() override {
        CHECK(!childDestroyed);
        childRenderDestroyed = true;
    }
};

class ChildWidget final : public Widget {
   public:
    RenderObject* createRenderObject() override { return new ChildRender(); }
};

class ChildApp final : public Application {
   public:
    const char* getAppName() override { return "child"; }
    Widget* buildUI() override { return new ChildWidget(); }
    void onDestroy() override { childDestroyNotified = true; }
    ~ChildApp() override {
        CHECK(childDestroyNotified && childRenderDestroyed);
        childDestroyed = true;
    }
};

class TestCanvas final : public Canvas {
   public:
    void clear() override {}
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t, int16_t, uint16_t) override {}
};
}  // namespace

int main() {
    // Install before constructing any Widget, RenderObject, or pooled vector;
    // this is required for accurate accounting in both storage profiles.
    MallocAdapter adapter;
    NanoUI::MemoryPool::instance().setAdapter(&adapter);

    TestCanvas canvas;
    WindowManager manager(&canvas, nullptr);
    RootApplication owner;

    manager.push(owner.buildUI(), &owner, rebuildRoot);
    CHECK(ownerBuilds == 1);
    CHECK(ownerWidgetsDestroyed == 1);  // owner widget released after RO creation
    CHECK(ownerButtonsDestroyed == 2);

    const NanoUI::StateKey stateKey = NanoUI::makeStateKey("unified-navigation");
    manager.saveState(stateKey, 73);
    manager.dispatchEvent({EVT_DOWN, 0});  // select the second root button
    CHECK(manager.getFocusedRenderObject() != nullptr);

    // Raw page widgets stay alive while another raw route is above them.
    manager.push(new CountingRow(rawRootWidgetsDestroyed, rawRootButtonsDestroyed));
    CHECK(rawRootWidgetsDestroyed == 0);
    CHECK(rawRootButtonsDestroyed == 0);
    manager.push(new CountingRow(rawNestedWidgetsDestroyed, rawNestedButtonsDestroyed));
    CHECK(rawNestedWidgetsDestroyed == 0);
    CHECK(rawNestedButtonsDestroyed == 0);
    manager.pop();
    CHECK(rawNestedWidgetsDestroyed == 1);
    CHECK(rawNestedButtonsDestroyed == 2);
    CHECK(rawRootWidgetsDestroyed == 0);
    CHECK(rawRootButtonsDestroyed == 0);
    manager.pop();

    // Returning to an owner page rebuilds its Widget tree, while its per-route
    // state and saved focus index survive the intervening raw routes.
    CHECK(rawRootWidgetsDestroyed == 1);
    CHECK(rawRootButtonsDestroyed == 2);
    CHECK(ownerBuilds == 2);
    CHECK(ownerWidgetsDestroyed == 2);
    CHECK(ownerButtonsDestroyed == 4);
    CHECK(manager.getStackSize() == 1);
    CHECK(manager.getState(stateKey, -1) == 73);
    CHECK(manager.getFocusedRenderObject() != nullptr);
    activated = 0;
    manager.dispatchEvent({EVT_OK, 0});
    CHECK(activated == 2);  // saved focus returned to the second root button

    // Product entry: returning through AppManager must reconstruct PopupRoot,
    // retain callback order, and destroy the outgoing render tree before App.
    AppManager::init(&canvas, nullptr);
    AppManager::launch(new AppRoot());
    CHECK(appRootBuilds == 1);
    CHECK(manager.getFocusedRenderObject()->parent != nullptr);
    AppManager::launch(new ChildApp());
    AppManager::exitCurrentApp();
    CHECK(childDestroyed && childRenderDestroyed && childDestroyNotified);
    CHECK(appRootBuilds == 2);
    CHECK(manager.getFocusedRenderObject()->parent != nullptr);

    // Report the active tree, then drain it before the local allocator expires.
    std::printf(
        "unified navigation failures=%d owner-builds=%d owner-widget-destroys=%d "
        "raw-root-destroys=%d raw-nested-destroys=%d active-render-bytes=%zu\n",
        failures, ownerBuilds, ownerWidgetsDestroyed, rawRootWidgetsDestroyed,
        rawNestedWidgetsDestroyed, NanoUI::MemoryPool::instance().getRenderUsage());
    AppManager::shutdown();
    manager.clear();
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    return failures ? 1 : 0;
}
