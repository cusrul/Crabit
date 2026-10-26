#pragma once

#include <NanoUI/foundation/owned.h>
#include <NanoUI/framework/types.h>
#include <NanoUI/framework/state.h>
#include <NanoUI/rendering/geometry.h>

namespace NanoUI {

class Widget;
class PaintCanvas;
enum class InputAction;
using WidgetPtr = Owned<Widget>;
namespace detail { class BuildOwnerImpl; }

class BuildOwner final {
public:
    explicit BuildOwner(Allocator&, OwnerOptions = {}) noexcept;
    ~BuildOwner() noexcept;
    BuildOwner(const BuildOwner&) = delete;
    BuildOwner& operator=(const BuildOwner&) = delete;
    BuildOwner(BuildOwner&&) = delete;
    BuildOwner& operator=(BuildOwner&&) = delete;

    UiError setRoot(WidgetPtr) noexcept;
    UiError clearRoot() noexcept;
    ElementHandle root() const noexcept;
    SnapshotResult inspect(ElementHandle) const noexcept;
    ElementHandle childAt(ElementHandle, std::size_t index) const noexcept;
    UiError setState(ElementHandle, StateMutation, void*) noexcept;
    UiError requestBuild(ElementHandle) noexcept;
    UiError setRootActive(bool) noexcept;
    PumpResult pump() noexcept;
    // Complete build before layout/paint; callbacks cannot mutate the tree here.
    FrameResult frame(PaintCanvas&, Size viewport) noexcept;
    UiError dispatchInput(ElementHandle, InputAction) noexcept;
    UiError setFocus(ElementHandle) noexcept; // Empty clears; only live enabled targets.
    ElementHandle focus() const noexcept;
    UiError focusNext(bool forward = true, bool wrap = true) noexcept;
    UiError dispatchFocused(InputAction) noexcept;
    // Empty restores root scope. A removed explicit scope fails closed until reset.
    UiError setInputScope(ElementHandle) noexcept;
    ElementHandle inputScope() const noexcept;
    bool atSafePoint() const noexcept;

private:
    Owned<detail::BuildOwnerImpl> impl_;
};

} // namespace NanoUI
