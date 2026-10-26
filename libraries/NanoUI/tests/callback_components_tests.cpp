#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
int failures = 0;
int globalNewCalls = 0;
#define CHECK(value)                                        \
    do {                                                    \
        if (!(value)) {                                     \
            ++failures;                                     \
            std::printf("line %d: %s\n", __LINE__, #value); \
        }                                                   \
    } while (false)

void plainCallback() {
}
int functionCalls = 0;
void countFunction() {
    ++functionCalls;
}
int boolCalls = 0;
bool lastBool = false;
void oldBoolCallback(bool value) {
    ++boolCalls;
    lastBool = value;
}
int floatCalls = 0;
float lastFloat = 0;
void oldFloatCallback(float value) {
    ++floatCalls;
    lastFloat = value;
}
int childWidgetDestroyed = 0;
int childRenderDestroyed = 0;

class CountedChildRender final : public RenderSizedBox {
   public:
    CountedChildRender() : RenderSizedBox(8, 8) {}
    ~CountedChildRender() override { ++childRenderDestroyed; }
};

class CountedChildWidget final : public Widget {
   public:
    ~CountedChildWidget() override { ++childWidgetDestroyed; }
    RenderObject* createRenderObject() override { return new CountedChildRender(); }
};

class Pixels final : public Canvas {
   public:
    unsigned char data[128 * 64]{};
    void clear() override { std::memset(data, 0, sizeof data); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= width() || y >= height())
            return;
        data[y * width() + x] = color == C_WHITE;
    }
};

class HeapAdapter final : public NanoUI::MemoryAdapter {
   public:
    void* alloc(size_t size) override { return std::malloc(size); }
    void free(void* ptr) override { std::free(ptr); }
};

struct Owner {
    explicit Owner(int value) : identity(value) {}
    void act() { ++calls; }
    int identity;
    int calls = 0;
};

class CallbackApp;

class TrackedRenderButton final : public RenderButton {
   public:
    TrackedRenderButton(NanoUI::Callback<void()> callback, bool& destroyed)
        : RenderButton(callback), destroyed_(&destroyed) {}
    ~TrackedRenderButton() override { *destroyed_ = true; }

   private:
    bool* destroyed_;
};

class TrackedButton final : public Button {
   public:
    TrackedButton(CallbackApp* app, bool& renderDestroyed);
    RenderObject* createRenderObject() override;

   private:
    CallbackApp* app_;
    bool* renderDestroyed_;
};

class CallbackApp final : public Application {
   public:
    CallbackApp(bool& alive, bool& renderDestroyed)
        : alive_(&alive), renderDestroyed_(&renderDestroyed) {
        *alive_ = true;
        *renderDestroyed_ = false;
    }
    ~CallbackApp() override {
        CHECK(*alive_);
        CHECK(*renderDestroyed_);  // manager removes the owned Render tree first
        *alive_ = false;
        ++destroyed;
    }
    const char* getAppName() override { return "callback-owner"; }
    Widget* buildUI() override { return new TrackedButton(this, *renderDestroyed_); }
    int calls = 0;
    static int destroyed;

   private:
    bool* alive_;
    bool* renderDestroyed_;
};
int CallbackApp::destroyed = 0;

TrackedButton::TrackedButton(CallbackApp* app, bool& renderDestroyed)
    : Button(new Text("run"), nullptr), app_(app), renderDestroyed_(&renderDestroyed) {
}

RenderObject* TrackedButton::createRenderObject() {
    CallbackApp* app = app_;
    TrackedRenderButton* render =
        new TrackedRenderButton([app]() { ++app->calls; }, *renderDestroyed_);
    if (child) {
        render->child = child->createRenderObject();
        render->child->parent = render;
    }
    return render;
}

class SelfDeletingButtonRender final : public RenderButton {
   public:
    explicit SelfDeletingButtonRender(NanoUI::Callback<void()> cb) : RenderButton(cb) {}
    ~SelfDeletingButtonRender() override { ++destroyed; }
    static int destroyed;
};
int SelfDeletingButtonRender::destroyed = 0;

class SelfDeletingButton final : public Widget {
   public:
    RenderObject* createRenderObject() override {
        SelfDeletingButtonRender* render = new SelfDeletingButtonRender(nullptr);
        int* counter = &externalCalls;
        render->onPressed = [render, counter]() {
            delete render;
            ++*counter;
        };
        return render;
    }
    static int externalCalls;
};
int SelfDeletingButton::externalCalls = 0;

void testCallbackValueSemantics() {
    NanoUI::Callback<void()> empty;
    CHECK(!empty);
    CHECK(empty.target() == nullptr);

    NanoUI::Callback<void()> function = &plainCallback;
    CHECK(function && function.target() == &plainCallback);
    function();

    NanoUI::Callback<void()> nullFunction = static_cast<VoidCallback>(nullptr);
    CHECK(!nullFunction && nullFunction.target() == nullptr);

    NanoUI::Callback<void()> noCapture = []() {};
    CHECK(noCapture && noCapture.target() != nullptr);
    noCapture();

    Owner owner(7);
    NanoUI::Callback<void()> captured = [&owner]() { owner.act(); };
    CHECK(captured && captured.target() == nullptr);
    NanoUI::Callback<void()> copied(captured);
    NanoUI::Callback<void()> assigned;
    assigned = captured;
    copied();
    assigned();
    CHECK(owner.identity == 7 && owner.calls == 2);

    const int state = 7;
    NanoUI::Callback<int()> valueCallback = [state]() { return state + 1; };
    NanoUI::Callback<int()> valueCopy(valueCallback);
    CHECK(valueCallback() == 8);
    CHECK(valueCallback() == 8);
    CHECK(valueCopy() == 8);

    int allocationsBefore = globalNewCalls;
    {
        Owner* value = &owner;
        NanoUI::Callback<void()> inlineValue = [value]() { value->act(); };
        NanoUI::Callback<void()> inlineCopy = inlineValue;
        NanoUI::Callback<void()> inlineAssigned;
        inlineAssigned = inlineCopy;
        inlineAssigned();
    }
    CHECK(globalNewCalls == allocationsBefore);
    CHECK(owner.calls == 3);
}

void testWidgetsAndIndependentOwners() {
    Owner first(11), second(22);
    Widget* firstWidget = new Button(new Text("first"), [&first]() { first.act(); });
    Widget* secondWidget = new Button(new Text("second"), [&second]() { second.act(); });
    RenderObject* firstRender = firstWidget->createRenderObject();
    RenderObject* secondRender = secondWidget->createRenderObject();
    delete firstWidget;
    delete secondWidget;
    firstRender->hasFocus = secondRender->hasFocus = true;
    CHECK(firstRender->handleEvent({EVT_OK, 0}));
    CHECK(secondRender->handleEvent({EVT_OK, 0}));
    CHECK(first.calls == 1 && second.calls == 1);
    delete firstRender;
    delete secondRender;

    // The callback's local copy keeps its inline closure alive when the
    // handler deletes the RenderButton whose member supplied the callback.
    Widget* selfWidget = new SelfDeletingButton();
    SelfDeletingButton::externalCalls = 0;
    RenderObject* selfRender = selfWidget->createRenderObject();
    delete selfWidget;
    selfRender->hasFocus = true;
    CHECK(selfRender->handleEvent({EVT_OK, 0}));
    CHECK(SelfDeletingButtonRender::destroyed == 1);
    CHECK(SelfDeletingButton::externalCalls == 1);
}

void testButtonVisualAndLegacyControls(Pixels& canvas) {
    VoidCallback legacyVoid = countFunction;
    BoolCallback legacyBool = oldBoolCallback;
    FloatCallback legacyFloat = oldFloatCallback;
    Widget* button = new Button(new Text("OK"), legacyVoid);
    RenderObject* buttonRender = button->createRenderObject();
    Text* referenceText = new Text("OK");
    RenderObject* referenceRender = referenceText->createRenderObject();
    delete referenceText;
    const BoxConstraints bounds = BoxConstraints::loose({64, 32});
    buttonRender->layout(bounds);
    referenceRender->layout(bounds);
    CHECK(buttonRender->size.width == referenceRender->size.width);
    CHECK(buttonRender->size.height == referenceRender->size.height);
    canvas.clear();
    buttonRender->paint(&canvas, {4, 5});
    unsigned char before[sizeof canvas.data];
    std::memcpy(before, canvas.data, sizeof before);
    buttonRender->hasFocus = true;
    CHECK(buttonRender->handleEvent({EVT_OK, 0}));
    CHECK(static_cast<RenderButton*>(buttonRender)->flashCounter == 3);
    canvas.clear();
    buttonRender->paint(&canvas, {4, 5});
    CHECK(std::memcmp(before, canvas.data, sizeof before) == 0);
    CHECK(functionCalls == 1);
    referenceRender->layout(bounds);
    canvas.clear();
    referenceRender->paint(&canvas, {4, 5});
    CHECK(std::memcmp(before, canvas.data, sizeof before) == 0);
    CHECK(buttonRender->getRadius() == referenceRender->getRadius());
    delete referenceRender;
    delete buttonRender;
    delete button;

    childWidgetDestroyed = childRenderDestroyed = 0;
    Button* owningButton = new Button(new CountedChildWidget(), nullptr);
    RenderObject* owningRender = owningButton->createRenderObject();
    delete owningButton;
    CHECK(childWidgetDestroyed == 1 && childRenderDestroyed == 0);
    delete owningRender;
    CHECK(childWidgetDestroyed == 1 && childRenderDestroyed == 1);

    bool enabled = false;
    Switch toggle(enabled, legacyBool);
    RenderObject* switchRender = toggle.createRenderObject();
    switchRender->hasFocus = true;
    CHECK(switchRender->handleEvent({EVT_OK, 0}));
    CHECK(enabled && boolCalls == 1 && lastBool);
    delete switchRender;

    float value = 5.0f;
    Slider threeArgument(value, 10.0f, legacyFloat);
    RenderObject* sliderRender = threeArgument.createRenderObject();
    sliderRender->hasFocus = true;
    CHECK(sliderRender->handleEvent({EVT_OK, 0}));
    CHECK(sliderRender->handleEvent({EVT_RIGHT, 0}));
    CHECK(value == 6.0f && floatCalls == 1 && lastFloat == 6.0f);
    delete sliderRender;

    value = 2.0f;
    Slider fourArgument(value, 10.0f, 80, legacyFloat);
    CHECK(fourArgument.width == 80);
    sliderRender = fourArgument.createRenderObject();
    sliderRender->hasFocus = true;
    CHECK(sliderRender->handleEvent({EVT_OK, 0}));
    CHECK(sliderRender->handleEvent({EVT_RIGHT, 0}));
    CHECK(value == 3.0f && floatCalls == 2 && lastFloat == 3.0f);
    delete sliderRender;
}

void testAppManagerCallbackLifetime(Pixels& canvas) {
    WindowManager manager(&canvas, nullptr);
    AppManager::init(&canvas, nullptr);
    bool rootAlive = false;
    bool childAlive = false;
    bool rootRenderDestroyed = false;
    bool childRenderDestroyed = false;
    CallbackApp::destroyed = 0;
    CallbackApp* root = new CallbackApp(rootAlive, rootRenderDestroyed);
    CallbackApp* child = new CallbackApp(childAlive, childRenderDestroyed);
    AppManager::launch(root);
    AppManager::launch(child);
    CHECK(rootAlive && childAlive);
    CHECK(manager.getStackSize() == 2);

    // The child's Button render callback and its borrowed App model are
    // both live until exitCurrentApp removes the Render tree, then destroys App.
    RenderObject* focused = manager.getFocusedRenderObject();
    CHECK(focused != nullptr);
    if (focused) {
        focused->hasFocus = true;
        CHECK(focused->handleEvent({EVT_OK, 0}));
    }
    CHECK(child->calls == 1);
    AppManager::exitCurrentApp();
    CHECK(rootAlive && !childAlive && childRenderDestroyed && CallbackApp::destroyed == 1);
    CHECK(manager.getStackSize() == 1);
    focused = manager.getFocusedRenderObject();
    CHECK(focused != nullptr);
    if (focused) {
        focused->hasFocus = true;
        CHECK(focused->handleEvent({EVT_OK, 0}));
    }
    CHECK(root->calls == 1);
    AppManager::shutdown();
    CHECK(!rootAlive && rootRenderDestroyed && CallbackApp::destroyed == 2);
}
}  // namespace

void* operator new(std::size_t size) {
    ++globalNewCalls;
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept {
    std::free(memory);
}

int main() {
    HeapAdapter heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    Pixels canvas;
    canvas.resetClipRect();
    testCallbackValueSemantics();
    testWidgetsAndIndependentOwners();
    testButtonVisualAndLegacyControls(canvas);
    testAppManagerCallbackLifetime(canvas);
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("callback components failures=%d\n", failures);
    return failures ? 1 : 0;
}
