// Characterization of the original, platform-owned WindowManager APIs.
// Historical fixture for snapshot 118ef69, NOT a test of the consolidated core.
// Current contract tests are in unified_navigation_tests.cpp.
// NANOUI_TEST_BW16 selects only the legacy include root/API and static definition.
#include <core/window_manager.h>
#include <framework/app.h>
#include <widgets/button.h>
#include <widgets/flex.h>
#include <widgets/basic.h>

#include <cstdio>
#include <cstdlib>

#ifdef NANOUI_TEST_BW16
WindowManager* WindowManager::instance = nullptr;
#else
#include <NanoUI/core/memory_pool.h>
class HeapAdapter final : public NanoUI::MemoryAdapter {
   public:
    void* alloc(size_t size) override { return std::malloc(size); }
    void free(void* ptr) override { std::free(ptr); }
};
#endif

namespace {
int failures = 0;
int pageBuilds = 0;
int buttonCreated = 0;
int buttonDestroyed = 0;
int activated = 0;

#define CHECK(value)                                        \
    do {                                                    \
        if (!(value)) {                                     \
            ++failures;                                     \
            std::printf("line %d: %s\n", __LINE__, #value); \
        }                                                   \
    } while (false)

class CountingButton final : public Button {
   public:
    explicit CountingButton(VoidCallback callback) : Button(new SizedBox(20, 10), callback) {
        ++buttonCreated;
    }
    ~CountingButton() override { ++buttonDestroyed; }
};

class OwnerPage final : public Application {
   public:
    const char* getAppName() override { return "lifecycle-probe"; }
    Widget* buildUI() override {
        ++pageBuilds;
        return new Row({new CountingButton(first), new CountingButton(second)});
    }
    static void first() { activated = 1; }
    static void second() { activated = 2; }
};

class PixelCanvas final : public Canvas {
   public:
    void clear() override {}
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t, int16_t, uint16_t) override {}
};

void savePageState(WindowManager& manager, int value) {
#ifdef NANOUI_TEST_BW16
    manager.saveState("probe", value);
#else
    manager.saveState(NanoUI::makeStateKey("probe"), value);
#endif
}

int readPageState(WindowManager& manager) {
#ifdef NANOUI_TEST_BW16
    return manager.getState("probe", -1);
#else
    return manager.getState(NanoUI::makeStateKey("probe"), -1);
#endif
}
}  // namespace

int main() {
#ifndef NANOUI_TEST_BW16
    HeapAdapter heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
#endif
    PixelCanvas canvas;
    WindowManager manager(&canvas, nullptr);

    // The root is a raw route on both ports and remains underneath the page.
    OwnerPage owner;
    manager.push(owner.buildUI()
#ifndef NANOUI_TEST_BW16
                     ,
                 &owner
#endif
    );
    savePageState(manager, 91);
    manager.dispatchEvent({EVT_DOWN, 0});
    CHECK(manager.getFocusedRenderObject() != nullptr);
    manager.dispatchEvent({EVT_DOWN, 0});
    // Two focusable controls wrap to the first. Save the focused index at one.
    manager.dispatchEvent({EVT_DOWN, 0});
    CHECK(manager.getFocusedRenderObject() != nullptr);

    Widget* ownerPage = owner.buildUI();
#ifdef NANOUI_TEST_BW16
    manager.push(ownerPage);
#else
    manager.push(ownerPage, &owner);
#endif

    CHECK(manager.getStackSize() == 2);
    RenderObject* initialFocus = manager.getFocusedRenderObject();
    CHECK(initialFocus != nullptr);
    manager.dispatchEvent({EVT_DOWN, 0});
    RenderObject* secondFocus = manager.getFocusedRenderObject();
    CHECK(secondFocus != nullptr && secondFocus != initialFocus);
    manager.dispatchEvent({EVT_OK, 0});
    CHECK(activated == 2);

    // Raw route configuration is retained until pop; the owner-backed STM32
    // route releases its Widget tree after constructing RenderObjects.
    const int destroyedBeforePop = buttonDestroyed;
#ifdef NANOUI_TEST_BW16
    CHECK(buttonDestroyed == 0);  // both raw route Widget trees are retained
#else
    CHECK(buttonDestroyed == 4);  // root and owner page Widgets released after RO creation
    CHECK(pageBuilds == 2);
#endif
    manager.pop();
    CHECK(manager.getStackSize() == 1);
#ifdef NANOUI_TEST_BW16
    CHECK(buttonDestroyed == destroyedBeforePop + 2);  // popped raw route is destroyed
    CHECK(pageBuilds == 2);  // owner output remains a normal retained Widget route
#else
    CHECK(buttonDestroyed == destroyedBeforePop + 2);
    CHECK(pageBuilds == 3);  // owner-backed root is reconstructed on return
#endif
    CHECK(readPageState(manager) == 91);
    CHECK(manager.getFocusedRenderObject() != nullptr);
    activated = 0;
    manager.dispatchEvent({EVT_OK, 0});
    CHECK(activated == 2);  // the root's saved focus index is restored

    // A further raw push/pop measures ordinary Widget destruction on each port.
    const int createdBeforeRaw = buttonCreated;
    const int destroyedBeforeRaw = buttonDestroyed;
    manager.push(
        new Row({new CountingButton(OwnerPage::first), new CountingButton(OwnerPage::second)}));
    CHECK(buttonCreated == createdBeforeRaw + 2);
#ifdef NANOUI_TEST_BW16
    CHECK(buttonDestroyed == destroyedBeforeRaw);
#else
    CHECK(buttonDestroyed == destroyedBeforeRaw + 2);
#endif
    manager.pop();
#ifdef NANOUI_TEST_BW16
    CHECK(buttonDestroyed == destroyedBeforeRaw + 2);
#else
    CHECK(buttonDestroyed == destroyedBeforeRaw + 4);  // raw child and reconstructed owner root
#endif

#ifndef NANOUI_TEST_BW16
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    const size_t activeRenderBytes = NanoUI::MemoryPool::instance().getRenderUsage();
    // Baseline limitation: WindowManager has no destructor for its active root.
    CHECK(activeRenderBytes > 0);
    std::printf("baseline active-render-bytes=%zu (WindowManager root is not released)\n",
                activeRenderBytes);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
#endif
    std::printf(
        "original navigation failures=%d builds=%d buttons-created=%d buttons-destroyed=%d\n",
        failures, pageBuilds, buttonCreated, buttonDestroyed);
    return failures ? 1 : 0;
}
