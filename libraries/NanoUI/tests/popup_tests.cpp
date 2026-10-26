#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
unsigned checks = 0;
#define CHECK(value)                                                 \
    do {                                                             \
        ++checks;                                                    \
        if (!(value)) {                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #value); \
            std::abort();                                            \
        }                                                            \
    } while (false)

class Heap final : public NanoUI::MemoryAdapter {
   public:
    unsigned blocks = 0;
    bool failNext = false;
    void* quarantined = nullptr;
    size_t quarantineBytes = 0;
    void* alloc(size_t size) override {
        if (failNext) {
            failNext = false;
            return nullptr;
        }
        void* pointer = std::malloc(size);
        if (pointer)
            ++blocks;
        return pointer;
    }
    void free(void* pointer) override {
        if (pointer) {
            CHECK(blocks);
            --blocks;
            if (pointer == quarantined)
                std::memset(pointer, 0xA5, quarantineBytes);
            else
                std::free(pointer);
        }
    }
    void releaseQuarantine() {
        for (size_t i = 0; i < quarantineBytes; ++i)
            CHECK(static_cast<unsigned char*>(quarantined)[i] == 0xA5);
        std::free(quarantined);
        quarantined = nullptr;
        quarantineBytes = 0;
    }
};

uint32_t ticks = 0;
uint32_t now() {
    return ticks;
}

class Screen final : public Canvas {
   public:
    void clear() override {}
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t, int16_t, uint16_t) override {}
};

class Keys final : public InputDriver {
   public:
    EventType next = EVT_NONE;
    void init() override {}
    UIEvent poll() override {
        UIEvent event{next, 0};
        next = EVT_NONE;
        return event;
    }
};

struct Probe {
    bool alive = true;
    bool failCreate = false;
    unsigned destroyed = 0;
    unsigned renders = 0;
    unsigned renderDeaths = 0;
    unsigned clicks = 0;
    std::string text = "borrowed text";
};

class ProbeRender final : public RenderObject {
   public:
    explicit ProbeRender(Probe& probe, Widget* redirect = nullptr, bool mutate = false)
        : probe_(probe), redirect_(redirect), mutate_(mutate) {
        ++probe_.renders;
    }
    ~ProbeRender() override {
        CHECK(probe_.alive);
        ++probe_.renderDeaths;
    }
    bool canFocus() override { return true; }
    bool handleEvent(Event event) override {
        CHECK(probe_.alive);
        if (event.type != EVT_OK || !hasFocus)
            return false;
        ++probe_.clicks;
        return true;
    }
    void performLayout() override {
        if (mutate_) {
            mutate_ = false;
            if (redirect_)
                Popup::show(redirect_);
            else
                Popup::hide();
        }
        size = constraints.constrainSize({24, 12});
    }
    void paintWorker(Canvas* canvas, Point point) override {
        CHECK(probe_.alive);
        canvas->drawText(point.x, point.y, probe_.text.c_str(), 8);
    }

   private:
    Probe& probe_;
    Widget* redirect_;
    bool mutate_;
};

class ProbeWidget final : public Widget {
   public:
    explicit ProbeWidget(Probe& probe) : probe_(probe) {}
    ~ProbeWidget() override {
        CHECK(probe_.alive);
        probe_.alive = false;
        ++probe_.destroyed;
    }
    RenderObject* createRenderObject() override {
        CHECK(probe_.alive);
        return probe_.failCreate ? nullptr : new ProbeRender(probe_);
    }

   private:
    Probe& probe_;
};

class RedirectWidget final : public Widget {
   public:
    RedirectWidget(Probe& probe, Widget* target, bool duringLayout)
        : probe_(probe), target_(target), duringLayout_(duringLayout) {}
    RenderObject* createRenderObject() override {
        if (!duringLayout_) {
            if (target_)
                Popup::show(target_);
            else
                Popup::hide();
        }
        return new ProbeRender(probe_, target_, duringLayout_);
    }

   private:
    Probe& probe_;
    Widget* target_;
    bool duringLayout_;
};

void checkExternalChildReplacement(Screen& screen, Heap& heap) {
    Probe original, replacement;
    WindowManager manager(&screen, nullptr);
    manager.push(new Container(new ProbeWidget(original)));
    manager.drawFrame();
    RenderObject* previous = manager.getFocusedRenderObject();
    CHECK(previous);
    RenderObject* ancestor = previous->parent;
    while (ancestor && !dynamic_cast<RenderContainer*>(ancestor))
        ancestor = ancestor->parent;
    auto* container = static_cast<RenderContainer*>(ancestor);
    CHECK(container && container->child == previous);
    heap.quarantined = previous;
    heap.quarantineBytes = sizeof(ProbeRender);
    delete container->child;  // Supported legacy direct Render child replacement.
    container->child = new ProbeRender(replacement);
    container->child->parent = container;
    container->markNeedsLayout();
    manager.drawFrame();
    heap.releaseQuarantine();  // Detect even a single stale hasFocus write.
    manager.dispatchEvent({EVT_OK, 0});
    CHECK(original.clicks == 0 && replacement.clicks == 1);
    manager.clear();
    CHECK(original.renderDeaths == 1 && replacement.renderDeaths == 1);
}

void checkLayoutMutation(Screen& screen) {
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        const unsigned mutation = scenario % 4;
        Probe source, destination;
        ProbeWidget destinationConfig(destination);
        RedirectWidget sourceConfig(source, mutation < 2 ? &destinationConfig : nullptr,
                                    (mutation & 1) != 0);
        Keys keys;
        WindowManager manager(&screen, &keys);
        manager.push(new PopupRoot(new SizedBox(8, 8)));
        Popup::show(&sourceConfig);
        manager.drawFrame();  // Factory/layout changes Popup while layout is active.
        CHECK(source.renders == 1 && source.renderDeaths == 0);
        if (scenario >= 4) {
            keys.next = EVT_OK;
            manager.update();  // No second draw: reconcile pending mutation before input.
            CHECK(source.clicks == 0 && source.renderDeaths == 1);
        }
        manager.drawFrame();  // Requested layout must survive RenderObject::layout returning.
        CHECK(source.renderDeaths == 1);
        if (mutation < 2) {
            CHECK(Popup::pendingContent == &destinationConfig && destination.renders == 1);
            manager.dispatchEvent({EVT_OK, 0});
            CHECK(destination.clicks == (scenario >= 4 ? 2u : 1u) && source.clicks == 0);
        } else
            CHECK(!Popup::isVisible && !manager.getFocusedRenderObject());
        manager.clear();
        Popup::reset();
        CHECK(destination.renders == destination.renderDeaths);
    }
}

void checkBorrowedReplacement(Screen& screen) {
    Probe first, second, base, rejected;
    {
        ProbeWidget firstConfig(first), secondConfig(second), rejectedConfig(rejected);
        Keys keys;
        WindowManager manager(&screen, &keys);
        manager.push(new PopupRoot(new ProbeWidget(base)));
        manager.drawFrame();
        RenderObject* baseFocus = manager.getFocusedRenderObject();
        CHECK(baseFocus && baseFocus->hasFocus);
        Popup::show(&firstConfig, 40, 20);
        manager.drawFrame();
        CHECK(first.renders == 1 && first.renderDeaths == 0);
        Popup::show(&secondConfig, 50, 22);
        keys.next = EVT_OK;
        manager.update();  // Real input path, no draw between replacement and OK.
        CHECK(first.renderDeaths == 1 && first.clicks == 0);
        CHECK(second.renders == 1 && second.clicks == 1 && second.destroyed == 0);
        Popup::hide();
        Popup::show(&secondConfig, 45, 20);  // Same address, no intervening layout.
        manager.dispatchEvent({EVT_OK, 0});
        CHECK(second.renders == 2 && second.renderDeaths == 1 && second.clicks == 2);
        Popup::hide();
        manager.dispatchEvent({EVT_OK, 0});
        CHECK(base.clicks == 1 && second.renderDeaths == 2);
        SizedBox noFocus(8, 8);
        Popup::show(&noFocus);
        keys.next = EVT_OK;
        manager.update();
        CHECK(!manager.getFocusedRenderObject() && !baseFocus->hasFocus);
        CHECK(base.clicks == 1);
        Popup::hide();
        manager.drawFrame();

        first.failCreate = true;
        Popup::show(&firstConfig);
        manager.dispatchEvent({EVT_OK, 0});
        CHECK(base.clicks == 1 && first.clicks == 0);  // Modal even if render creation failed.
        first.failCreate = false;
        Popup::show(&firstConfig);
        manager.drawFrame();
        CHECK(first.renders == 2);
        Popup::show(nullptr);
        manager.drawFrame();
        CHECK(!Popup::isVisible && first.renderDeaths == 2);

        for (unsigned i = 0; i < 1000; ++i) {
            Popup::show(&firstConfig);
            manager.drawFrame();
            Popup::show(&secondConfig);
            manager.drawFrame();
            Popup::hide();
            manager.drawFrame();
        }
        CHECK(first.renders == first.renderDeaths);
        CHECK(second.renders == second.renderDeaths);
        Popup::show(&firstConfig);
        manager.drawFrame();
        manager.clear();  // Render borrowers leave before reset and caller destruction.
        Popup::reset();
        CHECK(first.alive && second.alive && first.destroyed == 0 && second.destroyed == 0);
        CHECK(first.renders == first.renderDeaths);
        {
            NanoUI::detail::ShutdownGuard guard;
            Popup::show(&rejectedConfig);
            CHECK(rejected.alive && rejected.destroyed == 0 && !Popup::isVisible);
        }
    }
    CHECK(first.destroyed == 1 && second.destroyed == 1 && rejected.destroyed == 1);
    CHECK(base.destroyed == 1 && base.renders == base.renderDeaths);
}

void checkOwnedToast(Screen& screen, Heap& heap) {
    WindowManager manager(&screen, nullptr);
    manager.push(new PopupRoot(new SizedBox(20, 10)));
    ticks = 100;
    {
        std::string temporary = "temporary toast";
        Popup::showToast(temporary.c_str());
        temporary.assign(temporary.size(), 'x');
    }
    CHECK(std::strcmp(Popup::toastMsg, "temporary toast") == 0);
    manager.drawFrame();
    const char* chinese = "请在手机端打开天气并刷新";
    Popup::showToast(chinese);
    CHECK(std::strlen(chinese) == 36 && std::strcmp(Popup::toastMsg, chinese) == 0);
    Popup::showToast("temporary toast");
    Popup::showToast(Popup::toastMsg + 10);  // Alias into old storage; copy before free.
    CHECK(std::strcmp(Popup::toastMsg, "toast") == 0);
    const char* previous = Popup::toastMsg;
    const uint32_t previousEnd = Popup::toastEndTime;
    const unsigned previousBlocks = heap.blocks;
    heap.failNext = true;
    Popup::showToast("allocation failure");
    CHECK(Popup::toastMsg == previous && Popup::toastEndTime == previousEnd);
    CHECK(heap.blocks == previousBlocks);
    Popup::showToast(nullptr);
    CHECK(!Popup::toastMsg && Popup::toastEndTime == 0);
    Popup::showToast("zero", 0);
    Popup::showToast("negative", -1);
    Popup::showToast("");
    CHECK(!Popup::toastMsg);

    ticks = UINT32_MAX - 9;
    Popup::showToast("wrap", 20);
    ticks = UINT32_MAX - 1;
    manager.drawFrame();
    CHECK(Popup::toastMsg && std::strcmp(Popup::toastMsg, "wrap") == 0);
    ticks = 10;
    manager.drawFrame();
    CHECK(!Popup::toastMsg && Popup::toastEndTime == 0);

    for (unsigned i = 0; i < 1000; ++i) {
        Popup::showToast(std::string(80, 'a').c_str());  // No 32-byte pool-profile truncation.
        CHECK(std::strlen(Popup::toastMsg) == 80);
        Popup::showToast("replacement");
        ticks += 1500;
        manager.drawFrame();
        CHECK(!Popup::toastMsg);
    }
    Popup::showToast("reset");
    manager.clear();
    Popup::reset();
    Popup::reset();
    CHECK(!Popup::toastMsg && Popup::toastEndTime == 0);
}
}  // namespace

int main() {
    Heap heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(now, nullptr);
    Screen screen;
    checkBorrowedReplacement(screen);
    CHECK(heap.blocks == 0);
    checkExternalChildReplacement(screen, heap);
    checkLayoutMutation(screen);
    CHECK(heap.blocks == 0);
    checkOwnedToast(screen, heap);
    CHECK(heap.blocks == 0);
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("Popup: %u checks, borrowed ownership, replacement, focus and copied Toast PASS\n",
                checks);
}
