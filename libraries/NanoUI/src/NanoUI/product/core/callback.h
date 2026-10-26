#ifndef NANOUI_CALLBACK_H
#define NANOUI_CALLBACK_H

#include <new>
#include <type_traits>
#include <utility>

namespace NanoUI {

template <typename Signature>
class Callback;

template <typename Result, typename... Args>
class Callback<Result(Args...)> {
   public:
    using Function = Result (*)(Args...);

   private:
    Function function = nullptr;
    using Invoke = Result (*)(const void*, Args...);
    using Copy = void (*)(void*, const void*);
    Invoke invoke = nullptr;
    Copy copy = nullptr;
    alignas(void*) unsigned char storage[2 * sizeof(void*)] = {};

    template <typename Callable>
    void assignCallable(Callable&& callable, std::true_type) {
        // Captureless lambdas retain the original function-pointer path.
        function = callable;
    }

    template <typename Callable>
    void assignCallable(Callable&& callable, std::false_type) {
        using Stored = typename std::decay<Callable>::type;
        static_assert(sizeof(Stored) <= sizeof(storage), "NanoUI callback capture is too large");
        static_assert(alignof(Stored) <= alignof(void*),
                      "NanoUI callback capture alignment is unsupported");
        static_assert(std::is_trivially_copyable<Stored>::value,
                      "NanoUI callbacks require trivially copyable captures");
        static_assert(std::is_trivially_destructible<Stored>::value,
                      "NanoUI callbacks cannot own nontrivial captured resources");
        ::new (static_cast<void*>(storage)) Stored(std::forward<Callable>(callable));
        invoke = [](const void* payload, Args... args) -> Result {
            return (*static_cast<const Stored*>(payload))(std::forward<Args>(args)...);
        };
        copy = [](void* destination, const void* source) {
            ::new (destination) Stored(*static_cast<const Stored*>(source));
        };
    }

   public:
    constexpr Callback() = default;
    constexpr Callback(Function callback) : function(callback) {}

    template <
        typename Callable, typename Stored = typename std::decay<Callable>::type,
        typename std::enable_if<
            std::is_class<Stored>::value && !std::is_same<Stored, Callback>::value, int>::type = 0>
    Callback(Callable&& callable) : Callback() {
        assignCallable(std::forward<Callable>(callable), std::is_convertible<Stored, Function>{});
    }

    Callback(const Callback& other)
        : function(other.function), invoke(other.invoke), copy(other.copy) {
        if (copy)
            copy(storage, other.storage);
    }

    Callback& operator=(const Callback& other) {
        if (this == &other)
            return *this;
        function = other.function;
        invoke = other.invoke;
        copy = other.copy;
        if (copy)
            copy(storage, other.storage);
        return *this;
    }

    // Captures must be const-callable; mutable UI state belongs to the receiver,
    // not the closure copied for event dispatch. Captured pointers stay borrowed.
    // Invoke only a nonempty callback while its receiver/data are alive.
    Result operator()(Args... args) const {
        if (invoke)
            return invoke(storage, std::forward<Args>(args)...);
        return function(std::forward<Args>(args)...);
    }
    explicit operator bool() const { return function != nullptr || invoke != nullptr; }
    // There is no raw function target for a captured callable.
    Function target() const { return function; }
};

}  // namespace NanoUI

#endif
