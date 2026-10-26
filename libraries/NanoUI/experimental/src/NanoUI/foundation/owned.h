#pragma once

#include "allocator.h"
#include <new>
#include <type_traits>
#include <utility>

namespace NanoUI {

template<class T> class Owned;
template<class T, class... Args>
Owned<T> makeOwned(Allocator&, AllocationKind, Args&&...) noexcept;

// Retain the complete object independently of the view: base subobjects can
// have nonzero offsets. No RTTI or virtual destructor is required for ownership.
template<class T>
class Owned {
public:
    Owned() noexcept = default;
    ~Owned() { reset(); }
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    Owned(Owned&& other) noexcept { take(other); }
    Owned& operator=(Owned&& other) noexcept {
        if (this != &other) { reset(); take(other); }
        return *this;
    }
    template<class U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
    Owned(Owned<U>&& other) noexcept { take(other); }
    template<class U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
    Owned& operator=(Owned<U>&& other) noexcept { reset(); take(other); return *this; }

    T* get() const noexcept { return view_; }
    T* operator->() const noexcept { return view_; }
    T& operator*() const noexcept { return *view_; }
    explicit operator bool() const noexcept { return view_ != nullptr; }
    void reset() noexcept {
        if (complete_) destroy_(*allocator_, complete_, kind_);
        view_ = nullptr;
        complete_ = nullptr;
        allocator_ = nullptr;
        destroy_ = nullptr;
    }

private:
    template<class> friend class Owned;
    template<class U, class... Args>
    friend Owned<U> makeOwned(Allocator&, AllocationKind, Args&&...) noexcept;
#if defined(__cpp_noexcept_function_type)
    using Destroy = void (*)(Allocator&, void*, AllocationKind) noexcept;
#else
    // C++11 cannot encode noexcept in the pointer type; factories still only
    // install noexcept callbacks and reject throwing destructors above.
    using Destroy = void (*)(Allocator&, void*, AllocationKind);
#endif
    Owned(T* object, Allocator& allocator, AllocationKind kind, Destroy destroy) noexcept
        : view_(object), complete_(object), allocator_(&allocator), destroy_(destroy), kind_(kind) {}
    template<class U> void take(Owned<U>& other) noexcept {
        view_ = other.view_;
        complete_ = other.complete_;
        allocator_ = other.allocator_;
        destroy_ = other.destroy_;
        kind_ = other.kind_;
        other.view_ = nullptr;
        other.complete_ = nullptr;
        other.allocator_ = nullptr;
        other.destroy_ = nullptr;
    }
    T* view_ = nullptr;
    void* complete_ = nullptr;
    Allocator* allocator_ = nullptr;
    Destroy destroy_ = nullptr;
    AllocationKind kind_ = AllocationKind::widget;
};

template<class T, class... Args>
Owned<T> makeOwned(Allocator& allocator, AllocationKind kind, Args&&... args) noexcept {
    static_assert(std::is_nothrow_constructible<T, Args...>::value, "Factory constructors must be noexcept");
    static_assert(std::is_nothrow_destructible<T>::value, "Factory destructors must be noexcept");
    void* storage = allocator.allocate(sizeof(T), alignof(T), kind);
    if (!storage) return {};
    T* object = ::new (storage) T(std::forward<Args>(args)...);
    return Owned<T>(object, allocator, kind,
        [](Allocator& owner, void* pointer, AllocationKind allocationKind) noexcept {
            auto* complete = static_cast<T*>(pointer);
            complete->~T();
            owner.deallocate(pointer, sizeof(T), alignof(T), allocationKind);
        });
}

} // namespace NanoUI
