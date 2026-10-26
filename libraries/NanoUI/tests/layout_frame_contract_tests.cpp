// Regression contracts for frame scheduling, transition endpoints and focus
// report clipping in the original Widget/RenderObject WindowManager.
#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int failures = 0;
#define CHECK(expr)                                        \
    do {                                                   \
        if (!(expr)) {                                     \
            ++failures;                                    \
            std::printf("line %d: %s\n", __LINE__, #expr); \
        }                                                  \
    } while (false)

unsigned fakeTicks = 0;
unsigned getTicks() {
    return fakeTicks;
}

class HeapAdapter final : public NanoUI::MemoryAdapter {
   public:
    size_t allocations = 0;
    size_t releases = 0;
    size_t live = 0;
    void* alloc(size_t bytes) override {
        void* p = std::malloc(bytes);
        if (p) {
            ++allocations;
            ++live;
        }
        return p;
    }
    void free(void* p) override {
        if (p) {
            ++releases;
            --live;
            std::free(p);
        }
    }
};

class EmptyInput final : public InputDriver {
   public:
    void init() override {}
    UIEvent poll() override { return {EVT_NONE, 0}; }
};

class FrameCanvas final : public Canvas {
   public:
    int16_t w, h;
    uint8_t* pixels;
    unsigned displays = 0;
    FrameCanvas(int16_t width, int16_t height)
        : w(width), h(height), pixels(new uint8_t[static_cast<size_t>(width) * height]{}) {}
    FrameCanvas(const FrameCanvas&) = delete;
    FrameCanvas& operator=(const FrameCanvas&) = delete;
    FrameCanvas(FrameCanvas&& other) noexcept
        : w(other.w), h(other.h), pixels(other.pixels), displays(other.displays) {
        other.pixels = nullptr;
    }
    ~FrameCanvas() override { delete[] pixels; }
    void clear() override { std::memset(pixels, 0, static_cast<size_t>(w) * h); }
    void display() override { ++displays; }
    int16_t width() override { return w; }
    int16_t height() override { return h; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;
        uint8_t& pixel = pixels[static_cast<size_t>(y) * w + x];
        if (color == C_INVERT)
            pixel = static_cast<uint8_t>(!pixel);
        else
            pixel = static_cast<uint8_t>(color == C_WHITE);
    }
    bool equals(const FrameCanvas& other) const {
        return w == other.w && h == other.h &&
               std::memcmp(pixels, other.pixels, static_cast<size_t>(w) * h) == 0;
    }
};

class WhiteWidget final : public Widget {
   public:
    bool patterned;
    RenderObject* created = nullptr;
    explicit WhiteWidget(bool pattern = false) : patterned(pattern) {}
    RenderObject* createRenderObject() override;
};

class WhiteRender final : public RenderObject {
    bool patterned;

   public:
    explicit WhiteRender(bool pattern) : patterned(pattern) {}
    void performLayout() override {
        size = constraints.constrainSize({constraints.maxWidth, constraints.maxHeight});
    }
    void paintWorker(Canvas* canvas, Point p) override {
        canvas->fillRect(p.x, p.y, size.width, size.height, C_WHITE);
        if (patterned)
            for (int y = 0; y < size.height; ++y)
                for (int x = 0; x < size.width; ++x)
                    if ((x / 3 + y / 2) % 2)
                        canvas->drawPixelWithClip(p.x + x, p.y + y, C_BLACK);
    }
};

RenderObject* WhiteWidget::createRenderObject() {
    created = new WhiteRender(patterned);
    return created;
}

class TransitionWidget final : public SingleChildWidget {
   public:
    TransitionType type;
    RenderPageTransition* created = nullptr;
    TransitionWidget(Widget* child, TransitionType t) : SingleChildWidget(child), type(t) {}
    RenderObject* createRenderObject() override {
        created = new RenderPageTransition(type);
        if (child) {
            created->child = child->createRenderObject();
            created->child->parent = created;
        }
        return created;
    }
};

// Focus fixture with one valid report and an optional subsequent rejected
// report while a different nested clip is active.
class FocusReports final : public RenderObject {
   public:
    bool addRejectedReport = false;
    bool rejectAll = false;
    void performLayout() override {
        size = constraints.constrainSize({constraints.maxWidth, constraints.maxHeight});
    }
    void paintWorker(Canvas* canvas, Point p) override {
        canvas->fillRect(p.x, p.y, size.width, size.height, C_WHITE);
        WindowManager* wm = WindowManager::instance;
        if (rejectAll) {
            wm->reportFocusRect({-8, -8, -2, -2}, 0);
            wm->reportFocusRect({5, 5, 5, 9}, 0);
            return;
        }
        wm->reportFocusRect({8, 8, 24, 20}, 0);
        if (addRejectedReport) {
            ScopedClip outer(canvas, {30, 25, 45, 39});
            ScopedClip inner(canvas, {33, 28, 40, 35});
            wm->reportFocusRect({41, 29, 47, 35}, 0);  // outside the nested effective clip
            wm->reportFocusRect({36, 30, 36, 34},
                                0);  // empty, even though its span lies in the clip
        }
    }
};

class FocusWidget final : public Widget {
   public:
    bool appendRejected = false;
    bool rejectAll = false;
    FocusReports* created = nullptr;
    RenderObject* createRenderObject() override {
        created = new FocusReports();
        created->addRejectedReport = appendRejected;
        created->rejectAll = rejectAll;
        return created;
    }
};

void teardownManager(WindowManager& wm, RenderObject* trackedRoot, Widget* fixture) {
    // Clear the installed root through its owner before releasing borrowed
    // Widget configuration; WindowManager's destructor is also idempotent.
    (void)trackedRoot;
    wm.clear();
    delete fixture;
}

bool runTransition(TransitionType type, int16_t w, int16_t h, FrameCanvas& result,
                   float* finalValue = nullptr, bool pattern = false) {
    FrameCanvas canvas(w, h);
    EmptyInput input;
    WindowManager wm(&canvas, &input);
    TransitionWidget* fixture = new TransitionWidget(new WhiteWidget(pattern), type);
    wm.loadPage(fixture);
    RenderObject* trackedRoot = fixture->created;
    RenderPageTransition* transition = fixture->created;
    for (unsigned frame = 0; frame < 600; ++frame) {
        fakeTicks += 16;
        wm.drawFrame();
        if (transition->anim.value == transition->anim.target && transition->anim.isStable())
            break;
    }
    const bool reached = transition->anim.value == transition->anim.target;
    if (finalValue)
        *finalValue = transition->anim.value;
    const unsigned displaysAtEndpoint = canvas.displays;
    if (w > 0 && h > 0)
        CHECK(displaysAtEndpoint > 1);
    for (int i = 0; i < 8; ++i) {
        fakeTicks += 16;
        wm.drawFrame();
    }
    CHECK(canvas.displays == displaysAtEndpoint);
    result.clear();
    result.resetClipRect();
    WhiteRender unwrapped(pattern);
    unwrapped.layout(BoxConstraints::tight(w, h));
    unwrapped.paint(&result, {0, 0});
    const bool endpointMatchesWhite = canvas.equals(result);
    teardownManager(wm, trackedRoot, fixture);
    return reached && endpointMatchesWhite;
}

void testTransitions() {
    const TransitionType types[] = {TransitionType::SlideFromRight, TransitionType::CRT_Expand,
                                    TransitionType::CyberScan, TransitionType::FadeOutIn};
    for (TransitionType type : types) {
        FrameCanvas reference(128, 64);
        float finalValue = -1.0f;
        CHECK(runTransition(type, 128, 64, reference, &finalValue));
        CHECK(finalValue == 0.0f || finalValue == 1.0f);

        // Odd, non-default geometry catches truncated scan/tail endpoints.
        FrameCanvas oddReference(37, 19);
        CHECK(runTransition(type, 37, 19, oddReference));
        FrameCanvas smallReference(5, 3);
        CHECK(runTransition(type, 5, 3, smallReference));
        FrameCanvas zeroWidth(0, 8);
        CHECK(runTransition(type, 0, 8, zeroWidth));
        FrameCanvas zeroHeight(8, 0);
        CHECK(runTransition(type, 8, 0, zeroHeight));
        FrameCanvas patterned(37, 19);
        CHECK(runTransition(type, 37, 19, patterned, nullptr, true));
    }
}

void testTransitionResize() {
    for (TransitionType type : {TransitionType::SlideFromRight, TransitionType::CRT_Expand,
                                TransitionType::CyberScan, TransitionType::FadeOutIn}) {
        FrameCanvas canvas(128, 64);  // backing allocation remains large enough
        EmptyInput input;
        WindowManager wm(&canvas, &input);
        TransitionWidget* fixture = new TransitionWidget(new WhiteWidget(true), type);
        wm.loadPage(fixture);
        RenderPageTransition* node = fixture->created;
        for (int i = 0; i < 5; ++i) {
            fakeTicks += 16;
            wm.drawFrame();
        }
        const float progress = node->anim.value;
        CHECK(progress != node->anim.target);
        canvas.w = 37;
        canvas.h = 19;
        wm.markNeedsLayout();
        wm.performLayoutAndFocus();
        CHECK(node->anim.value == progress);  // relayout must not restart progress
        for (int i = 0; i < 600; ++i) {
            fakeTicks += 16;
            wm.drawFrame();
        }
        CHECK(node->anim.value == node->anim.target);
        FrameCanvas expected(37, 19);
        expected.resetClipRect();
        WhiteRender reference(true);
        reference.layout(BoxConstraints::tight(37, 19));
        reference.paint(&expected, {0, 0});
        CHECK(canvas.equals(expected));
        canvas.w = 13;
        canvas.h = 7;
        wm.markNeedsLayout();
        fakeTicks += 16;
        wm.drawFrame();
        CHECK(node->anim.value == node->anim.target);
        FrameCanvas small(13, 7);
        small.resetClipRect();
        reference.markNeedsLayout();
        reference.layout(BoxConstraints::tight(13, 7));
        reference.paint(&small, {0, 0});
        CHECK(canvas.equals(small));
        teardownManager(wm, node, fixture);
    }
}

FrameCanvas renderFocusCase(bool appendRejected, bool allRejected, Rect* finalClip = nullptr) {
    FrameCanvas canvas(64, 48);
    canvas.resetClipRect();
    EmptyInput input;
    WindowManager wm(&canvas, &input);
    FocusWidget* fixture = new FocusWidget();
    fixture->appendRejected = appendRejected;
    fixture->rejectAll = allRejected;
    wm.loadPage(fixture);
    RenderObject* trackedRoot = fixture->created;
    wm.drawFrame();
    if (finalClip)
        *finalClip = canvas.getClipRect();

    // Return pixels through a heap-independent copied snapshot before fixture
    // cleanup; callers own the returned canvas and its pixel storage.
    FrameCanvas snapshot(64, 48);
    std::memcpy(snapshot.pixels, canvas.pixels, sizeof(uint8_t) * 64 * 48);
    teardownManager(wm, trackedRoot, fixture);
    return snapshot;
}

void testFocusReports() {
    Rect clipAfter = {};
    FrameCanvas singleValid = renderFocusCase(false, false);
    FrameCanvas validThenRejected = renderFocusCase(true, false, &clipAfter);
    CHECK(singleValid.pixels[12 * 64 + 16] == 0);  // a valid overlay actually drew
    CHECK(singleValid.equals(validThenRejected));
    CHECK(clipAfter.left == 0 && clipAfter.top == 0 && clipAfter.right == 64 &&
          clipAfter.bottom == 48);
    FrameCanvas allRejected = renderFocusCase(false, true);
    FrameCanvas unchangedWhite(64, 48);
    unchangedWhite.fillRect(0, 0, 64, 48, C_WHITE);
    CHECK(allRejected.equals(unchangedWhite));

    EmptyInput input;
    FrameCanvas canvas(64, 48);
    WindowManager wm(&canvas, &input);
    FocusWidget* fixture = new FocusWidget();
    wm.loadPage(fixture);
    fakeTicks += 16;
    wm.drawFrame();
    CHECK(canvas.pixels[12 * 64 + 16] == 0);
    fixture->created->rejectAll = true;
    wm.markDirty();
    fakeTicks += 16;
    wm.drawFrame();
    CHECK(canvas.equals(unchangedWhite));  // prior frame's target cannot survive rejection
    teardownManager(wm, fixture->created, fixture);
}
}  // namespace

int main() {
    HeapAdapter heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(getTicks, nullptr);
    testTransitions();
    testTransitionResize();
    testFocusReports();
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
    CHECK(heap.live == 0);
    CHECK(heap.allocations == heap.releases);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    WindowManager::instance = nullptr;
    std::printf("layout frame contract failures=%d\n", failures);
    return failures ? 1 : 0;
}
