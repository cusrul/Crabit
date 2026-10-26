#include <NanoUI.h>
#include "components/companion_pair_prompt.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

static size_t globalBlocks = 0;
void* operator new(size_t size) {
    void* p = std::malloc(size ? size : 1);
    if (!p)
        throw std::bad_alloc();
    ++globalBlocks;
    return p;
}
void operator delete(void* p) noexcept {
    if (p) {
        --globalBlocks;
        std::free(p);
    }
}
void operator delete(void* p, size_t) noexcept {
    ::operator delete(p);
}
void* operator new[](size_t size) {
    return ::operator new(size);
}
void operator delete[](void* p) noexcept {
    ::operator delete(p);
}
void operator delete[](void* p, size_t) noexcept {
    ::operator delete(p);
}

static unsigned checks = 0;
#define CHECK(expr)                                                 \
    do {                                                            \
        ++checks;                                                   \
        if (!(expr)) {                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); \
            std::exit(1);                                           \
        }                                                           \
    } while (false)

class Heap final : public NanoUI::MemoryAdapter {
   public:
    size_t blocks = 0;
    void* alloc(size_t n) override {
        void* p = std::malloc(n);
        if (p)
            ++blocks;
        return p;
    }
    void free(void* p) override {
        if (p) {
            --blocks;
            std::free(p);
        }
    }
};
class Pixels final : public Canvas {
   public:
    unsigned displays = 0;
    void clear() override {}
    void display() override { ++displays; }
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t, int16_t, uint16_t) override {}
};
class Keys final : public InputDriver {
   public:
    EventType next = EVT_NONE;
    void init() override {}
    UIEvent poll() override {
        const auto e = next;
        next = EVT_NONE;
        return {e, 0};
    }
};
class Anchor final : public Application {
   public:
    const char* getAppName() override { return "PAIR TEST"; }
    Widget* buildUI() override { return new SizedBox(1, 1); }
};

static uint32_t timeMs = 0;
static uint32_t now() {
    return timeMs;
}
static void frame(WindowManager& wm) {
    timeMs += 16;
    wm.drawFrame();
}
static void key(WindowManager& wm, Keys& keys, EventType e) {
    keys.next = e;
    wm.update();
}
static uint16_t currentSession = 7, calledSession = 0;
static uint32_t currentPair = 0, calledPair = 0;
static unsigned allows = 0, denies = 0;
static bool sendWorks = true;
static bool validate(uint16_t s, uint32_t p) {
    calledSession = s;
    calledPair = p;
    return sendWorks && s == currentSession && p && p == currentPair;
}
static bool allow(uint16_t s, uint32_t p) {
    ++allows;
    return validate(s, p);
}
static bool deny(uint16_t s, uint32_t p) {
    ++denies;
    return validate(s, p);
}

static RenderPopupRoot* popupRoot(WindowManager& wm) {
    RenderObject* p = wm.getFocusedRenderObject();
    while (p && !dynamic_cast<RenderPopupRoot*>(p))
        p = p->parent;
    CHECK(p);
    return static_cast<RenderPopupRoot*>(p);
}
static RenderSlidingText* foundLabel = nullptr;
static void findLabel(RenderObject* p) {
    if (auto* label = dynamic_cast<RenderSlidingText*>(p))
        foundLabel = label;
    p->visitChildren(findLabel);
}
static RenderSlidingText* labelIn(RenderObject* p) {
    foundLabel = nullptr;
    findLabel(p);
    CHECK(foundLabel);
    return foundLabel;
}

static_assert(!std::is_copy_constructible<Crabit::CompanionPairPrompt>::value, "owner cannot copy");
static_assert(!std::is_move_constructible<Crabit::CompanionPairPrompt>::value,
              "callbacks borrow owner");
static_assert(!std::is_copy_assignable<Crabit::CompanionPairPrompt>::value, "owner cannot assign");
static_assert(!std::is_move_assignable<Crabit::CompanionPairPrompt>::value,
              "owner cannot move assign");

int main() {
    Heap heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(now, nullptr);
    const size_t initialGlobal = globalBlocks;
    {
        Crabit::CompanionPairPrompt prompt(allow, deny);
        Pixels pixels;
        Keys keys;
        {
            WindowManager wm(&pixels, &keys);
            AppManager::init(&pixels, &keys);
            AppManager::launch(new Anchor());
            frame(wm);
            prompt.tick(0, 1);
            prompt.tick(7, 0);
            CHECK(!Popup::isVisible);
            currentPair = UINT32_MAX;
            prompt.tick(currentSession, currentPair);
            Widget* const config = Popup::pendingContent;
            CHECK(config && Popup::isVisible);
            frame(wm);
            auto* root = popupRoot(wm);
            CHECK(root->popup);
            CHECK(std::strcmp(labelIn(root->popup)->dataSource->c_str(),
                              "PAIR PHONE? #4294967295") == 0);
            prompt.invalidate(currentSession, currentPair);
            CHECK(Popup::isVisible);  // Matching invalidation leaves own popup intact.

            // Rejected send retains visible UI. Service performs validation.
            sendWorks = false;
            key(wm, keys, EVT_OK);
            CHECK(allows == 1 && calledSession == 7 && calledPair == UINT32_MAX);
            CHECK(Popup::isVisible);
            sendWorks = true;
            prompt.dismiss();
            frame(wm);
            prompt.tick(7, UINT32_MAX);  // consume external-dismiss gap
            CHECK(!root->popup && !Popup::isVisible);

            // Warm the retained string/vector capacities before exact block counts.
            size_t visibleHeap = 0, hiddenHeap = 0, visibleGlobal = 0, hiddenGlobal = 0;
            size_t widgetBytes = 0;
            for (uint32_t i = 0; i < 1000; ++i) {
                currentPair = 100000 + i * 2;
                prompt.tick(currentSession, currentPair);
                frame(wm);
                CHECK(Popup::isVisible && Popup::pendingContent == config && root->popup);
                RenderSlidingText* old = labelIn(root->popup);
                char expected[32];
                std::snprintf(expected, sizeof expected, "PAIR PHONE? #%lu",
                              static_cast<unsigned long>(currentPair));
                CHECK(std::strcmp(old->dataSource->c_str(), expected) == 0);
                if (!i) {
                    visibleHeap = heap.blocks;
                    visibleGlobal = globalBlocks;
                    widgetBytes = NanoUI::MemoryPool::instance().getWidgetUsage();
                }
                CHECK(heap.blocks == visibleHeap && globalBlocks == visibleGlobal);
                CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == widgetBytes);

                const uint32_t previous = currentPair++;
                // invalidate hides only; old render and its source remain unchanged.
                prompt.invalidate(currentSession, currentPair);
                CHECK(!Popup::isVisible && !Popup::pendingContent && root->popup);
                CHECK(std::strcmp(old->dataSource->c_str(), expected) == 0);
                // A still-live old render callback sends its OLD correlation;
                // the service rejects it after the real pair sequence changed.
                auto* button = dynamic_cast<RenderButton*>(wm.getFocusedRenderObject());
                CHECK(button);
                button->onPressed();
                CHECK(calledPair == previous && calledSession == currentSession);
                prompt.tick(currentSession, currentPair);  // guaranteed hide-only tick
                CHECK(!Popup::isVisible && root->popup);
                CHECK(std::strcmp(old->dataSource->c_str(), expected) == 0);
                frame(wm);
                CHECK(!root->popup);  // actual framework removed old render BEFORE mutation
                prompt.tick(currentSession, currentPair);
                CHECK(Popup::isVisible && Popup::pendingContent == config);
                frame(wm);
                CHECK(root->popup);
                CHECK(heap.blocks == visibleHeap && globalBlocks == visibleGlobal);
                if (i & 1)
                    key(wm, keys, EVT_DOWN);
                key(wm, keys, EVT_OK);
                CHECK(calledSession == currentSession && calledPair == currentPair);
                CHECK(!Popup::isVisible);
                frame(wm);
                CHECK(!root->popup);
                prompt.tick(currentSession, currentPair);
                prompt.tick(currentSession, currentPair);
                CHECK(!Popup::isVisible);  // accepted request is not re-offered each frame
                if (!i) {
                    hiddenHeap = heap.blocks;
                    hiddenGlobal = globalBlocks;
                }
                CHECK(heap.blocks == hiddenHeap && globalBlocks == hiddenGlobal);
            }
            CHECK(denies == 500 && allows >= 500);

            // Session change with equal sequence also requires a hidden frame.
            ++currentPair;
            prompt.tick(7, currentPair);
            frame(wm);
            prompt.tick(8, currentPair);
            CHECK(!Popup::isVisible && root->popup);
            frame(wm);
            CHECK(!root->popup);
            currentSession = 8;
            prompt.tick(8, currentPair);
            frame(wm);
            key(wm, keys, EVT_OK);
            CHECK(calledSession == 8 && calledPair == currentPair);
            frame(wm);
            prompt.tick(8, currentPair);

            // A different owner's visible popup must never be hidden/overwritten.
            std::unique_ptr<Widget> other(new Button(new Text("OTHER", 8), []() {}));
            Popup::show(other.get(), 80, 24);
            frame(wm);
            ++currentPair;
            prompt.tick(8, currentPair);
            prompt.invalidate(0, 0);
            prompt.dismiss();
            CHECK(Popup::isVisible && Popup::pendingContent == other.get());
            Popup::hide();
            frame(wm);
            other.reset();  // unrelated configuration owner releases after its render
            prompt.tick(8, currentPair);
            frame(wm);
            CHECK(Popup::isVisible && Popup::pendingContent == config);
            prompt.invalidate(8, 0);
            CHECK(!Popup::isVisible && root->popup);
            frame(wm);
            CHECK(!root->popup);
            prompt.tick(8, 0);

            // Shutdown without a prior dismiss: Popup must not delete the
            // configuration still owned by prompt's unique_ptr.
            prompt.tick(8, ++currentPair);
            frame(wm);
            CHECK(root->popup);
            CHECK(root->popup && Popup::pendingContent == config);
            AppManager::shutdown();
            CHECK(wm.getStackSize() == 0 && !Popup::pendingContent);
            CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
            CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == widgetBytes);
        }  // WindowManager dies before prompt.
    }  // component configuration and retained label storage now die.
    CHECK(heap.blocks == 0 && globalBlocks == initialGlobal);
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf(
        "companion pair prompt: 1000 cycles, %u checks, zero live heap blocks after teardown\n",
        checks);
}
