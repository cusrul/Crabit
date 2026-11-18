#ifndef APP_FACTORY_H
#define APP_FACTORY_H

#include "../../src/NanoUI.h"
#include <new>
#include <type_traits>
#include <utility>

// Narrow creation seam for catalogue ports and ordinary menu apps.
//
// The Arduboy runtime migration owns the managed definition (resource
// preparation and rollback) in compat; this header only declares and adapts it:
// - a managed port exposes the public prepareForLaunch() hook, so the generic
//   menu factory calls CrabitArduboy::tryCreate<T>() and gets nullptr on
//   failure, before AppManager mounts anything;
// - an ordinary Application is constructed with new (std::nothrow); a returned
//   nullptr is never launched. This is not a recoverable target-OOM guarantee:
//   the stock BW16 allocator calls a non-returning failure hook on exhaustion.
//
// Contract:
//   template <typename T> T* tryCreate();
// - T derives from Application;
// - returns a fully prepared T (not yet mounted by AppManager);
// - if allocation returns failure, rolls back partial preparation and returns
//   nullptr; the stock target allocator can halt before returning.
//
// The definition must be visible where the managed factory is instantiated
// (the app headers in app/arduboy30_registry.h include the Arduboy compat
// headers).
namespace CrabitArduboy {

template <typename T>
T* tryCreate();

namespace detail {

// Duck-typed on purpose: including ArduboyCompatApp here would couple ordinary
// apps to compat. prepareForLaunch() is the documented public managed hook.
template <typename T>
struct HasPrepareForLaunch {
    template <typename U>
    static auto test(int) -> decltype(std::declval<U&>().prepareForLaunch(),
                                      std::true_type());
    template <typename>
    static std::false_type test(...);
    static const bool value = decltype(test<T>(0))::value;
};

}  // namespace detail

// One factory for every registry entry: managed ports prepare resources first;
// ordinary Applications use a failable constructor. Callers launch only when
// the result is non-null.
template <typename T>
typename std::enable_if<detail::HasPrepareForLaunch<T>::value, Application*>::type
createForMenu() {
    return tryCreate<T>();
}

template <typename T>
typename std::enable_if<!detail::HasPrepareForLaunch<T>::value, Application*>::type
createForMenu() {
    return new (std::nothrow) T();
}

}  // namespace CrabitArduboy

#endif
