#pragma once

#include <NanoUI/foundation/owned.h>
#include <NanoUI/framework/types.h>

namespace NanoUI {

class BuildContext;
class Widget;
struct BuildResult;

#if defined(__cpp_noexcept_function_type)
using StateMutation = void (*)(class State&, void*) noexcept;
#else
// The no-throw callback contract remains required on SDKs lacking noexcept types.
using StateMutation = void (*)(class State&, void*);
#endif

class State {
public:
    virtual ~State() noexcept;
    virtual void initState(BuildContext&) noexcept;
    virtual void didUpdateWidget(const Widget& previous, BuildContext&) noexcept;
    virtual BuildResult build(BuildContext&) noexcept = 0;
    virtual void dispose() noexcept;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;

protected:
    State() noexcept = default;
};

} // namespace NanoUI
