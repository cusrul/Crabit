#pragma once

#include <NanoUI/foundation/identity.h>
#include <cstddef>
#include <cstdint>

namespace NanoUI {

class BuildOwner;

enum class UiError { none, outOfMemory, invalidInput, staleHandle,
                     invalidPhase, capacityExceeded, buildFailed };
enum class WidgetKind { leaf, stateless, stateful, rendering };
enum class ElementPhase { created, active, inactive, unmounted };

struct ElementHandle {
    constexpr ElementHandle(const BuildOwner* ownerValue = nullptr,
                            std::uint64_t serialValue = 0) noexcept
        : owner(ownerValue), serial(serialValue) {}
    const BuildOwner* owner = nullptr;
    std::uint64_t serial = 0;
};

struct OwnerOptions {
    constexpr OwnerOptions(std::size_t depth = 64, std::size_t builds = 32,
                           std::size_t children = 64) noexcept
        : maxDepth(depth), maxBuildsPerOperation(builds), maxChildren(children) {}
    std::size_t maxDepth = 64;
    std::size_t maxBuildsPerOperation = 32;
    std::size_t maxChildren = 64;
};

struct PumpResult {
    UiError error = UiError::none;
    std::size_t builds = 0;
    bool needsFrame = false;
};

struct FrameResult {
    UiError error = UiError::none;
    std::size_t builds = 0;
    bool painted = false;
    bool needsFrame = false;
};

struct ElementSnapshot {
    ElementPhase phase = ElementPhase::unmounted;
    TypeId type = typeId<void>();
    Key key{};
    ElementHandle child{};
    bool dirty = false;
    bool hasState = false;
    bool focusable = false;
    std::size_t childCount = 0;
};

struct SnapshotResult {
    UiError error = UiError::none;
    ElementSnapshot value{};
};

} // namespace NanoUI
