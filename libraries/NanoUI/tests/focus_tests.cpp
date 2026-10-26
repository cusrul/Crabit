#include <NanoUI/widgets/input_region.h>
#include <NanoUI/navigation/navigator.h>
#include <windows/heap_allocator.h>
#include <cstdio>
namespace {
using namespace NanoUI;
int checks = 0, failures = 0;
#define CHECK(x)                                   \
    do {                                           \
        ++checks;                                  \
        if (!(x)) {                                \
            ++failures;                            \
            std::printf("%d: %s\n", __LINE__, #x); \
        }                                          \
    } while (false)
struct Model {
    BuildOwner* owner = nullptr;
    int gained = 0, lost = 0, actions = 0;
    static bool action(InputAction a, void* payload) noexcept {
        auto& m = *static_cast<Model*>(payload);
        if (a != InputAction::activate)
            return false;
        ++m.actions;
        return true;
    }
    static void focus(bool value, void* payload) noexcept {
        auto& m = *static_cast<Model*>(payload);
        value ? ++m.gained : ++m.lost;
        CHECK(m.owner->clearRoot() == UiError::invalidPhase);
        CHECK(m.owner->setFocus({}) == UiError::invalidPhase);
        CHECK(m.owner->setInputScope({}) == UiError::invalidPhase);
        CHECK(m.owner->dispatchFocused(InputAction::activate) == UiError::invalidPhase);
        CHECK(m.owner->requestBuild(m.owner->root()) == UiError::none);
    }
};
struct MenuModel {
    Model rows[3];
    bool reverse = false, removeLast = false, disableFirst = false;
};
class Menu final : public StatelessWidget {
   public:
    explicit Menu(MenuModel& m) noexcept : StatelessWidget(typeId<Menu>()), model(m) {}
    BuildResult build(BuildContext& c) const noexcept override {
        BuildResult r;
        for (int n = 0; n < 3; ++n) {
            const int i = model.reverse ? 2 - n : n;
            if (i == 2 && model.removeLast)
                continue;
            WidgetPtr item =
                makeOwned<InputRegion>(c.allocator(), AllocationKind::widget, Model::action,
                                       &model.rows[i], ChildBuilder{}, Key::value(i + 1),
                                       i != 1 && !(i == 0 && model.disableFirst), Model::focus);
            if (!item) {
                r.error = UiError::outOfMemory;
                return r;
            }
            r.error = r.children.append(c.allocator(), item);
            if (r.error != UiError::none)
                return r;
        }
        return r;
    }

   private:
    MenuModel& model;
};
void bind(MenuModel& model, BuildOwner* owner) {
    for (auto& row : model.rows)
        row.owner = owner;
}
void traversalAndScope() {
    Windows::HeapAllocator heap;
    TrackingAllocator a(heap);
    MenuModel model;
    {
        BuildOwner owner(a);
        bind(model, &owner);
        CHECK(owner.setRoot(makeOwned<Menu>(a, AllocationKind::widget, model)) == UiError::none);
        const auto first = owner.childAt(owner.root(), 0);
        const auto disabled = owner.childAt(owner.root(), 1);
        const auto third = owner.childAt(owner.root(), 2);
        CHECK(owner.inspect(first).value.focusable);
        CHECK(!owner.inspect(disabled).value.focusable);
        CHECK(owner.setFocus(disabled) == UiError::invalidInput);
        CHECK(owner.focusNext() == UiError::none && owner.focus().serial == first.serial);
        CHECK(model.rows[0].gained == 1);
        CHECK(owner.dispatchFocused(InputAction::activate) == UiError::none &&
              model.rows[0].actions == 1);
        CHECK(owner.dispatchFocused(InputAction::next) == UiError::none &&
              owner.focus().serial == third.serial);
        CHECK(model.rows[0].lost == 1 && model.rows[2].gained == 1);
        CHECK(owner.focusNext() == UiError::none && owner.focus().serial == first.serial);
        CHECK(owner.focusNext(false, false) == UiError::none &&
              owner.focus().serial == first.serial);
        CHECK(owner.focusNext(false) == UiError::none && owner.focus().serial == third.serial);
        // Keyed reorder retains the same target, traversal follows new tree order.
        model.reverse = true;
        CHECK(owner.requestBuild(owner.root()) == UiError::none &&
              owner.pump().error == UiError::none);
        CHECK(owner.focus().serial == third.serial);
        CHECK(owner.focusNext() == UiError::none && owner.focus().serial == first.serial);
        CHECK(owner.setInputScope(third) == UiError::none && !owner.focus().serial);
        CHECK(owner.inputScope().serial == third.serial);
        CHECK(owner.dispatchInput(first, InputAction::activate) == UiError::invalidInput);
        CHECK(owner.setFocus(first) == UiError::invalidInput);
        CHECK(owner.focusNext() == UiError::none && owner.focus().serial == third.serial);
        CHECK(owner.focusNext() == UiError::none && owner.focus().serial == third.serial);
        // Removing a modal scope must not silently reopen the background menu.
        model.removeLast = true;
        CHECK(owner.requestBuild(owner.root()) == UiError::none &&
              owner.pump().error == UiError::none);
        CHECK(!owner.focus().serial && !owner.inputScope().serial);
        CHECK(owner.focusNext() == UiError::invalidInput);
        CHECK(owner.dispatchInput(first, InputAction::activate) == UiError::invalidInput);
        CHECK(owner.setInputScope({}) == UiError::none && owner.focusNext() == UiError::none);
        CHECK(owner.focus().serial == first.serial);
        model.disableFirst = true;
        CHECK(owner.requestBuild(owner.root()) == UiError::none &&
              owner.pump().error == UiError::none);
        CHECK(!owner.focus().serial && owner.focusNext() == UiError::invalidInput);
        BuildOwner foreign(a);
        CHECK(foreign.setFocus(first) == UiError::staleHandle);
        CHECK(owner.setRootActive(false) == UiError::none);
        CHECK(owner.focusNext() == UiError::invalidInput);
        CHECK(owner.dispatchInput(first, InputAction::activate) == UiError::invalidInput);
        CHECK(owner.setRootActive(true) == UiError::none);
        CHECK(owner.clearRoot() == UiError::none);
        CHECK(owner.setFocus(first) == UiError::staleHandle);
    }
    CHECK(a.statistics().currentBlocks() == 0);
}
void routeIsolation() {
    Windows::HeapAllocator heap;
    TrackingAllocator a(heap);
    MenuModel background, modal;
    {
        Navigator nav(a);
        WidgetPtr root = makeOwned<Menu>(a, AllocationKind::widget, background);
        CHECK(nav.push(root).status == NavigationStatus::queued);
        CHECK(nav.commit().status == NavigationStatus::complete);
        auto* old = nav.current();
        bind(background, old);
        CHECK(nav.dispatchInput(InputAction::next) == UiError::none);
        const auto retained = old->focus();
        root = makeOwned<Menu>(a, AllocationKind::widget, modal);
        CHECK(nav.push(root).status == NavigationStatus::queued);
        CHECK(nav.commit().status == NavigationStatus::complete);
        bind(modal, nav.current());
        CHECK(old->dispatchInput(retained, InputAction::activate) == UiError::invalidInput);
        CHECK(nav.dispatchInput(InputAction::activate) == UiError::invalidInput);
        CHECK(background.rows[0].actions == 0);
        CHECK(nav.dispatchInput(InputAction::next) == UiError::none);
        CHECK(nav.dispatchInput(InputAction::activate) == UiError::none &&
              modal.rows[0].actions == 1);
        CHECK(nav.dispatchInput(InputAction::back) == UiError::invalidInput);
        CHECK(background.rows[0].actions == 0);  // Unhandled input does not bubble.
        CHECK(nav.pop().status == NavigationStatus::queued);
        CHECK(nav.commit().status == NavigationStatus::complete);
        CHECK(nav.current() == old && old->focus().serial == retained.serial);
        CHECK(nav.dispatchInput(InputAction::activate) == UiError::none &&
              background.rows[0].actions == 1);
    }
    CHECK(a.statistics().currentBlocks() == 0);
}
}  // namespace
int main() {
    traversalAndScope();
    routeIsolation();
    std::printf("focus: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
