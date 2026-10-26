#include <NanoUI/framework/build_owner.h>
#include <NanoUI/navigation/navigator.h>
#include <NanoUI/widgets/text.h>
#include <NanoUI/components/list_tile.h>
#include <NanoUI/components/dialog.h>
#include <NanoUI/rendering/diagnostic_font.h>
#include <NanoUI/widgets/scroll_controller.h>
#include <windows/heap_allocator.h>
#include <windows/raster_canvas.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace NanoUI;
enum class Kind { text, column, row, sized, padded, box, scroll, tile, button, dialog };
struct Node {
    explicit Node(Kind kind) : kind(kind) {}
    Kind kind;
    std::size_t id = 0;
    std::string text;
    std::uint32_t color = 0xFFFFFFFFu;
    float width = 0, height = 0, gap = 0, scroll = 0;
    Insets padding;
    BoxStyle style;
    TilePresentation tile;
    DialogPresentation dialog;
    std::vector<Node*> children;
    WidgetPtr make(Allocator& a) noexcept {
        const Key key = Key::value(id);
        const ChildBuilder content(build, this);
        switch (kind) {
        case Kind::text: return makeText(a, TextView(text.c_str(), text.size()), diagnosticFont(), 7, color, key, TextOverflow::ellipsis);
        case Kind::column: return makeOwned<Linear>(a, AllocationKind::widget, content,
            Axis::vertical, gap, CrossAlign::stretch, key);
        case Kind::row: return makeOwned<Linear>(a, AllocationKind::widget, content,
            Axis::horizontal, gap, CrossAlign::center, key, MainAlign::spaceBetween);
        case Kind::sized: return makeOwned<SizedBox>(a, AllocationKind::widget, Size(width, height), content, key);
        case Kind::padded: return makeOwned<Padding>(a, AllocationKind::widget, padding, content, key);
        case Kind::box: return makeOwned<DecoratedBox>(a, AllocationKind::widget, style, content, key);
        case Kind::scroll: return makeOwned<Scroll>(a, AllocationKind::widget, scroll, height, content, key);
        case Kind::tile:
            tile.contents = content; return makeOwned<ListTile>(a, AllocationKind::widget, tile, key);
        case Kind::button:
            tile.contents = content; return makeOwned<Button>(a, AllocationKind::widget, tile, key);
        case Kind::dialog:
            dialog.body = content; return makeOwned<Dialog>(a, AllocationKind::widget, dialog, key);
        }
        return {};
    }
    static BuildResult build(BuildContext& c, void* payload) noexcept {
        auto& node = *static_cast<Node*>(payload);
        BuildResult result;
        for (auto* child : node.children) {
            auto widget = child->make(c.allocator());
            if (!widget) { result.error = UiError::outOfMemory; return result; }
            result.error = result.children.append(c.allocator(), widget);
            if (result.error != UiError::none) return result;
        }
        return result;
    }
};
class Scene {
public:
    Node& node(Kind kind) {
        nodes.emplace_back(new Node(kind));
        nodes.back()->id = nodes.size(); return *nodes.back();
    }
    Node& text(const char* value) { auto& n = node(Kind::text); n.text = value; return n; }
    Node& line(const char* value) {
        auto& n = node(Kind::padded); n.padding = Insets(3, 0, 3, 0);
        n.children.push_back(&text(value)); return n;
    }
    Node& column(float gap = 0) { auto& n = node(Kind::column); n.gap = gap; return n; }
    Node& tile(const char* label, bool selected = false, bool button = false) {
        auto& n = node(button ? Kind::button : Kind::tile);
        n.tile.selected = selected;
        auto& t = text(label); t.color = selected ? 0x000000FFu : 0xFFFFFFFFu;
        n.children.push_back(&t); return n;
    }
    Node& setting(const char* label, const char* value, bool selected) {
        auto& n = node(Kind::tile); n.tile.selected = selected;
        auto& row = node(Kind::row);
        for (const auto* content : {label, value}) {
            auto& t = text(content); t.color = selected ? 0x000000FFu : 0xFFFFFFFFu;
            row.children.push_back(&t);
        }
        n.children.push_back(&row); return n;
    }
    Node& shell(Node& content, float width, float height) {
        auto& box = node(Kind::box); box.style = BoxStyle(0x000000FFu); box.children.push_back(&content);
        auto& size = node(Kind::sized); size.width = width; size.height = height;
        size.children.push_back(&box); return size;
    }
private:
    std::vector<std::unique_ptr<Node>> nodes; // Recipes outlive their Widgets and Elements.
};

class FailAllocator final : public Allocator {
public:
    void* allocate(std::size_t bytes, std::size_t alignment, AllocationKind kind) noexcept override {
        if (remaining == 0) return nullptr;
        if (remaining > 0) --remaining;
        return heap.allocate(bytes, alignment, kind);
    }
    void deallocate(void* p, std::size_t bytes, std::size_t alignment, AllocationKind kind) noexcept override {
        heap.deallocate(p, bytes, alignment, kind);
    }
    int remaining = -1;
    Windows::HeapAllocator heap;
};
int failures = 0, checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    std::fprintf(stderr, "%d: %s\n", __LINE__, #value); } } while (false)

ElementHandle find(BuildOwner& owner, ElementHandle parent, Key key) {
    const auto s = owner.inspect(parent);
    if (s.error != UiError::none) return {};
    if (s.value.key == key && s.value.type == typeId<InputRegion>()) return parent;
    for (std::size_t i = 0; i < s.value.childCount; ++i) {
        auto result = find(owner, owner.childAt(parent, i), key);
        if (result.serial) return result;
    }
    return {};
}
struct ActionState { BuildOwner* owner; int count = 0; UiError clear = UiError::none; };
bool activate(InputAction action, void* payload) noexcept {
    auto& state = *static_cast<ActionState*>(payload);
    if (action != InputAction::activate) return false;
    ++state.count;
    state.clear = state.owner->clearRoot(); // Deletion during input must be rejected.
    return true;
}

void renderPage(const char* output, const char* name, unsigned width, unsigned height, int scenario) {
    Scene scene;
    auto& body = scene.column(1);
    auto& heading = scene.node(Kind::padded); heading.padding = Insets(3, 2, 3, 2);
    const char* titles[] = {"Home", "Settings", "Settings", "Edit value", "Confirm",
        "Radio demo", "Connecting", "Networks", "Connection", "Networks"};
    heading.children.push_back(&scene.text(titles[scenario])); body.children.push_back(&heading);
    Node* button = nullptr;
    if (scenario == 0) {
        body.children.push_back(&scene.tile("Settings", true));
        body.children.push_back(&scene.tile("Bluetooth"));
        body.children.push_back(&scene.tile("Diagnostics"));
    } else if (scenario == 1 || scenario == 2) {
        auto& list = scene.column(1);
        const char* labels[] = {"BLE", "Wi-Fi", "USB", "Brightness", "Timeout", "About"};
        const char* values[] = {"ON", "OFF", "UART", "80", "30s", ">"};
        for (int i = 0; i < 6; ++i) list.children.push_back(&scene.setting(labels[i], values[i], i == (scenario == 1 ? 0 : 4)));
        auto& scroll = scene.node(Kind::scroll); scroll.height = static_cast<float>(height) - 13;
        scroll.scroll = scenario == 1 ? 0 : 48; scroll.children.push_back(&list); body.children.push_back(&scroll);
    } else if (scenario == 3) {
        body.children.push_back(&scene.tile("Value: 80", true));
        body.children.push_back(&scene.tile("-     +     OK"));
        body.children.push_back(&scene.line("BACK: cancel"));
    } else if (scenario == 4) {
        body.children.push_back(&scene.line("Apply changes?"));
        button = &scene.tile("CONFIRM", true, true); body.children.push_back(button);
        body.children.push_back(&scene.tile("Cancel"));
    } else if (scenario == 5) {
        body.children.push_back(&scene.line("BLE: READY"));
        body.children.push_back(&scene.line("PAIR: RAM OK"));
        body.children.push_back(&scene.line("LINK: 12 ms"));
        body.children.push_back(&scene.tile("RUN PROBE", true));
    } else if (scenario == 6) {
        body.children.push_back(&scene.line("Connecting..."));
        body.children.push_back(&scene.tile("Cancel", true));
    } else if (scenario == 7) {
        body.children.push_back(&scene.line("No networks"));
        body.children.push_back(&scene.tile("Scan again", true));
    } else if (scenario == 8) {
        body.children.push_back(&scene.line("Connection failed"));
        body.children.push_back(&scene.tile("Retry", true));
        body.children.push_back(&scene.line("BACK: leave"));
    } else {
        body.children.push_back(&scene.tile("A very long network title", true));
        body.children.push_back(&scene.tile("Short name"));
    }
    Node* page = &body;
    if (scenario == 4) {
        body.children.erase(body.children.begin()); // Confirmation lives in its own bounded dialog surface.
        auto& dialog = scene.node(Kind::dialog); dialog.children.push_back(&body); page = &dialog;
    }
    auto& root = scene.shell(*page, static_cast<float>(width), static_cast<float>(height));
    FailAllocator upstream;
    TrackingAllocator allocator(upstream);
    RasterCanvas canvas(width, height);
    {
        ActionState action{nullptr};
        BuildOwner owner(allocator, OwnerOptions(63, 512, 64));
        action.owner = &owner;
        if (button) { button->tile.action = activate; button->tile.actionPayload = &action; }
        auto widget = root.make(allocator);
        const auto mounted = owner.setRoot(std::move(widget));
        if (mounted != UiError::none) std::fprintf(stderr, "%s mount error %d\n", name, static_cast<int>(mounted));
        CHECK(mounted == UiError::none);
        const auto first = owner.frame(canvas, Size(static_cast<float>(width), static_cast<float>(height)));
        CHECK(first.error == UiError::none && first.painted && canvas.balanced() && !canvas.error());
        if (button) CHECK(canvas.pixel(9, height / 2) == 0xFFFFFFFFu); // Selected action must actually be visible.
        std::size_t white = 0;
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
            if (canvas.pixel(x, y) == 0xFFFFFFFFu) ++white;
        CHECK(white > 20 && white < static_cast<std::size_t>(width) * height);
        const std::string path = std::string(output) + "/" + name + "-" + std::to_string(width) + "x" + std::to_string(height) + ".ppm";
        CHECK(canvas.writePpm(path.c_str()));
        const auto rootHandle = owner.root();
        CHECK(owner.requestBuild(rootHandle) == UiError::none);
        const auto again = owner.frame(canvas, Size(static_cast<float>(width), static_cast<float>(height)));
        CHECK(again.error == UiError::none && again.painted && owner.root().serial == rootHandle.serial);
        if (button) {
            const auto target = find(owner, owner.root(), Key::value(button->id));
            CHECK(target.serial != 0);
            CHECK(owner.dispatchInput(target, InputAction::activate) == UiError::none);
            CHECK(action.count == 1 && action.clear == UiError::invalidPhase);
            CHECK(owner.dispatchInput(target, InputAction::back) == UiError::invalidInput);
        }
        upstream.remaining = 0;
        CHECK(owner.requestBuild(rootHandle) == UiError::none);
        CHECK(owner.frame(canvas, Size(static_cast<float>(width), static_cast<float>(height))).error == UiError::outOfMemory);
        CHECK(owner.root().serial == rootHandle.serial);
        upstream.remaining = -1;
        CHECK(owner.frame(canvas, Size(static_cast<float>(width), static_cast<float>(height))).error == UiError::none);
        CHECK(owner.clearRoot() == UiError::none);
        CHECK(owner.inspect(rootHandle).error == UiError::staleHandle);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
}
// Scripted host input targets an explicit region. This is not a global focus manager.
struct Interaction {
    Navigator* nav;
    Allocator* allocator;
    Node* choices[2];
    Node* dialog;
    int selected = 0;
};
void interactionFocus(bool, void* payload) noexcept {
    auto& model = *static_cast<Interaction*>(payload);
    auto& owner = *model.nav->current();
    const auto selected = owner.inspect(owner.focus());
    for (int i = 0; i < 2; ++i) {
        const bool match = selected.error == UiError::none && selected.value.key == Key::value(model.choices[i]->id);
        model.choices[i]->tile.selected = match;
        model.choices[i]->children[0]->color = match ? 0x000000FFu : 0xFFFFFFFFu;
        if (match) model.selected = i;
    }
    CHECK(owner.requestBuild(owner.root()) == UiError::none);
}
bool interact(InputAction action, void* payload) noexcept {
    auto& model = *static_cast<Interaction*>(payload);
    if (action == InputAction::activate) {
        auto widget = model.dialog->make(*model.allocator);
        return model.nav->push(widget).status == NavigationStatus::queued;
    }
    if (action == InputAction::back) return model.nav->pop().status == NavigationStatus::queued;
    return false;
}
void renderInteraction(const char* output, unsigned height) {
    Scene scene;
    auto& menu = scene.column(1);
    menu.children.push_back(&scene.line("Home"));
    auto& settings = scene.tile("Settings", true, true);
    auto& bluetooth = scene.tile("Bluetooth", false, true);
    menu.children.push_back(&settings); menu.children.push_back(&bluetooth);
    auto& home = scene.shell(menu, 128, static_cast<float>(height));
    auto& body = scene.column(1);
    body.children.push_back(&scene.line("Open Bluetooth?"));
    auto& cancel = scene.tile("BACK: cancel", true, true);
    body.children.push_back(&cancel);
    auto& surface = scene.node(Kind::dialog); surface.children.push_back(&body);
    auto& dialog = scene.shell(surface, 128, static_cast<float>(height));
    Windows::HeapAllocator heap;
    TrackingAllocator a(heap);
    RasterCanvas canvas(128, height);
    {
        Navigator nav(a);
        Interaction model{&nav, &a, {&settings, &bluetooth}, &dialog};
        for (Node* button : {&settings, &bluetooth, &cancel}) {
            button->tile.action = interact; button->tile.actionPayload = &model;
        }
        settings.tile.focusChanged = interactionFocus;
        bluetooth.tile.focusChanged = interactionFocus;
        auto root = home.make(a);
        CHECK(nav.push(root).status == NavigationStatus::queued);
        CHECK(nav.commit().status == NavigationStatus::complete);
        CHECK(nav.current()->setFocus(find(*nav.current(), nav.current()->root(), Key::value(settings.id))) == UiError::none);
        const auto save = [&](const char* step) {
            const auto frame = nav.current()->frame(canvas, Size(128, static_cast<float>(height)));
            CHECK(frame.error == UiError::none && frame.painted && !canvas.error());
            const std::string path = std::string(output) + "/interaction/" + step + "-128x" + std::to_string(height) + ".ppm";
            CHECK(canvas.writePpm(path.c_str()));
        };
        save("01-settings-selected");
        CHECK(nav.dispatchInput(InputAction::next) == UiError::none);
        save("02-bluetooth-selected");
        std::vector<std::uint32_t> expected;
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < 128; ++x)
            expected.push_back(canvas.pixel(x, y));
        CHECK(nav.dispatchInput(InputAction::activate) == UiError::none);
        CHECK(nav.pending() == 1 && nav.depth() == 1);
        CHECK(nav.commit().status == NavigationStatus::complete);
        save("03-confirm-route");
        auto target = find(*nav.current(), nav.current()->root(), Key::value(cancel.id));
        CHECK(nav.current()->setFocus(target) == UiError::none);
        CHECK(nav.dispatchInput(InputAction::back) == UiError::none);
        CHECK(nav.commit().status == NavigationStatus::complete);
        save("04-return-selection-retained");
        bool restored = true;
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < 128; ++x)
            restored = restored && canvas.pixel(x, y) == expected[y * 128 + x];
        CHECK(restored && model.selected == 1);
        CHECK(!nav.current()->frame(canvas, Size(128, static_cast<float>(height))).painted);
    }
    CHECK(a.statistics().currentBlocks() == 0);
}
struct FocusRow {
    BuildOwner* owner = nullptr;
    Node* button = nullptr;
    Node* scroll = nullptr;
    ScrollController* controller = nullptr;
    int index = 0;
    float viewport = 0;
    static bool action(InputAction action, void*) noexcept { return action == InputAction::activate; }
    static void focus(bool selected, void* payload) noexcept {
        auto& row = *static_cast<FocusRow*>(payload);
        row.button->tile.selected = selected;
        row.button->children[0]->color = selected ? 0x000000FFu : 0xFFFFFFFFu;
        if (selected) {
            CHECK(row.controller->reveal(static_cast<float>(row.index * 16),
                static_cast<float>(row.index * 16 + 15), row.viewport, 95) == UiError::none);
            row.scroll->scroll = row.controller->offset();
        }
        CHECK(row.owner->requestBuild(row.owner->root()) == UiError::none);
    }
};
void renderFocusScroll(const char* output, unsigned height) {
    Scene scene;
    auto& body = scene.column(1);
    auto& heading = scene.node(Kind::padded); heading.padding = Insets(3, 2, 3, 2);
    heading.children.push_back(&scene.text("Focus & scroll")); body.children.push_back(&heading);
    auto& list = scene.column(1);
    auto& scroll = scene.node(Kind::scroll); scroll.height = static_cast<float>(height) - 13;
    scroll.children.push_back(&list); body.children.push_back(&scroll);
    auto& root = scene.shell(body, 128, static_cast<float>(height));
    Windows::HeapAllocator heap;
    TrackingAllocator a(heap);
    ScrollController controller;
    RasterCanvas canvas(128, height);
    FocusRow rows[6]; // Payloads outlive owner/Elements; no stack recipe inside build.
    const char* labels[] = {"BLE", "Wi-Fi (disabled)", "USB", "Brightness", "Timeout", "About"};
    {
        BuildOwner owner(a, OwnerOptions(63, 512, 64));
        for (int i = 0; i < 6; ++i) {
            auto& button = scene.tile(labels[i], false, true);
            rows[i].owner = &owner; rows[i].button = &button; rows[i].scroll = &scroll;
            rows[i].controller = &controller; rows[i].index = i; rows[i].viewport = scroll.height;
            button.tile.action = FocusRow::action; button.tile.actionPayload = &rows[i];
            button.tile.focusChanged = FocusRow::focus; button.tile.enabled = i != 1;
            list.children.push_back(&button);
        }
        CHECK(owner.setRoot(root.make(a)) == UiError::none);
        const auto save = [&](const char* step, int index) {
            const auto frame = owner.frame(canvas, Size(128, static_cast<float>(height)));
            CHECK(frame.error == UiError::none && frame.painted && !canvas.error());
            CHECK(owner.focus().serial == find(owner, owner.root(), Key::value(rows[index].button->id)).serial);
            const float y = 13 + static_cast<float>(index * 16) - controller.offset();
            CHECK(y >= 13 && y + 15 <= static_cast<float>(height));
            CHECK(canvas.pixel(0, static_cast<unsigned>(y + 1)) == 0xFFFFFFFFu);
            const std::string path = std::string(output) + "/focus/" + step + "-128x" + std::to_string(height) + ".ppm";
            CHECK(canvas.writePpm(path.c_str()));
        };
        CHECK(owner.focusNext() == UiError::none); save("01-first", 0);
        CHECK(owner.dispatchFocused(InputAction::next) == UiError::none); save("02-disabled-skipped", 2);
        CHECK(owner.dispatchFocused(InputAction::next) == UiError::none); save("03-brightness", 3);
        CHECK(owner.dispatchFocused(InputAction::next) == UiError::none); save("04-timeout", 4);
        CHECK(owner.dispatchFocused(InputAction::next) == UiError::none); save("05-last-revealed", 5);
        CHECK(owner.dispatchFocused(InputAction::next) == UiError::none); save("06-wrap-first", 0);
        CHECK(owner.dispatchFocused(InputAction::previous) == UiError::none); save("07-wrap-last", 5);
        CHECK(!owner.frame(canvas, Size(128, static_cast<float>(height))).painted);
    }
    CHECK(a.statistics().currentBlocks() == 0);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: nanoui_ui_gallery existing-output-directory\n"); return 2; }
    const char* names[] = {"launcher", "settings", "settings-scroll", "edit", "confirm",
        "diagnostics", "loading", "empty", "failed", "long-title"};
    for (unsigned height : {64u, 96u})
        for (int scenario = 0; scenario < 10; ++scenario)
            renderPage(argv[1], names[scenario], 128, height, scenario);
    for (unsigned height : {64u, 96u}) renderInteraction(argv[1], height);
    for (unsigned height : {64u, 96u}) renderFocusScroll(argv[1], height);
    std::printf("UI gallery: %d checks, %d failures; 20 static, 8 route, 14 focus/scroll frames\n", checks, failures);
    return failures ? 1 : 0;
}
