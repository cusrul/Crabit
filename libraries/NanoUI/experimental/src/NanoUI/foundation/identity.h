#pragma once

#include <cstdint>
#include <type_traits>

namespace NanoUI {

namespace detail {
// Function-local static storage gives every canonical type one program-wide
// address while remaining compatible with pre-C++17 compilers.
template<class T> const void* typeToken() noexcept {
    static const unsigned char token = 0;
    return &token;
}
}

class TypeId {
public:
    constexpr explicit TypeId(const void* token) noexcept : token_(token) {}
    friend constexpr bool operator==(TypeId a, TypeId b) noexcept { return a.token_ == b.token_; }
    friend constexpr bool operator!=(TypeId a, TypeId b) noexcept { return !(a == b); }
private:
    const void* token_;
};

// Identity is local to one linked process, never a wire/persistent identifier.
template<class T> TypeId typeId() noexcept {
    typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type Canonical;
    return TypeId(detail::typeToken<Canonical>());
}

class Key {
public:
    constexpr Key() noexcept = default;
    static constexpr Key value(std::uint64_t value) noexcept { return Key(value); }
    constexpr bool hasValue() const noexcept { return present_; }
    friend constexpr bool operator==(Key a, Key b) noexcept {
        return a.present_ == b.present_ && (!a.present_ || a.value_ == b.value_);
    }
    friend constexpr bool operator!=(Key a, Key b) noexcept { return !(a == b); }
private:
    constexpr explicit Key(std::uint64_t value) noexcept : value_(value), present_(true) {}
    std::uint64_t value_ = 0;
    bool present_ = false;
};

} // namespace NanoUI
