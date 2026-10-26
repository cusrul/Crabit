#pragma once

#include <NanoUI/foundation/allocator.h>
#include <NanoUI/framework/types.h>

namespace NanoUI {

class BuildOwner;
class Widget;
namespace detail { class Element; class BuildOwnerImpl; }

class BuildContext final {
public:
    const Widget& widget() const noexcept;
    ElementHandle handle() const noexcept;
    Allocator& allocator() const noexcept;
    BuildOwner& owner() const noexcept;
    BuildContext(const BuildContext&) = delete;
    BuildContext& operator=(const BuildContext&) = delete;
    BuildContext(BuildContext&&) = delete;
    BuildContext& operator=(BuildContext&&) = delete;

private:
    friend class detail::Element;
    friend class detail::BuildOwnerImpl;
    BuildContext(const Widget&, ElementHandle, Allocator&, BuildOwner&) noexcept;
    const Widget* widget_;
    ElementHandle handle_;
    Allocator* allocator_;
    BuildOwner* owner_;
};

} // namespace NanoUI
