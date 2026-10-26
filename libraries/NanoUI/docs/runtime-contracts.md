# NanoUI runtime contracts

These are the detailed storage, callback and lifetime notes retained from the
library README during the public documentation reorganization. They describe
the maintained original framework at source snapshot `549f3a3`, not a new runtime
implementation or a complete guarantee of failure recovery.

## Storage and navigation compatibility

The generic `NANOUI_DYNAMIC_STORAGE` configuration chooses a storage backend,
not a second UI implementation. Default 1 preserves standard heap allocation,
vectors, strings and exact string state keys (BW16). Configuration 0 preserves
STM32's existing injected pool, bounded containers/text and numeric keys.
It does not turn dynamic Widget/RenderObject allocation into static allocation.
Use one consistent setting for the library and all consumers; CMake publishes it.

App-owned routes reconstruct via their Application and release configuration
after creating RenderObjects. Raw Widget routes retain configuration until pop,
so existing ownerless Navigator pushes can return. AppManager reconstructs its
PopupRoot wrapper too, and releases a departing route before destroying its App.
Application state and borrowed text must outlive the corresponding RenderObjects.
The original single-context/non-thread-safe model remains unchanged.

### Object callbacks and composed controls

Button, Switch and Slider accept `NanoUI::Callback` values. Existing function
pointer callbacks, `nullptr`, and Slider's three-argument callback form remain
valid. The legacy `VoidCallback`, `BoolCallback` and `FloatCallback` function
pointer typedefs are unchanged. A member receiver can now be expressed directly:

```cpp
new Switch(enabled, [this](bool value) { applyEnabled(value); });
```

Captured callables must be const-callable, trivially copyable/destructible, fit
within two pointer-widths, and require no more than pointer alignment. Unsupported
captures fail compilation; there is no fallback allocation. Keep changing state
in the App, not in a mutable closure. Captureless lambdas use the function-pointer
path; `target()` returns that function pointer and is null for captured callables.
Function-pointer construction remains constexpr; Callback copying is now a
runtime operation, not a constexpr copy guarantee. Inline callback storage does
not change Widget/RenderObject dynamic allocation or the platform allocator.
The wrapper occupies five pointer-widths on the tested Windows GCC ABI (20 bytes
on i686, 40 on x64), including inline capture storage, so it is not zero-overhead
relative to the former raw function pointers. Widget-to-Render copies own the
closure value, but captured `this`/data pointers are still borrowed.

Controls copy their callback locally before invocation, so deleting the Render
during that invocation does not invalidate the closure's storage. This does not
keep a captured App alive or fix arbitrary self-exit callbacks: do not access a
receiver after destroying it. Existing manager lifetime rules still apply.

RenderButton reuses RenderSingleChildBox's child destruction, traversal, painting
and radius forwarding; layout, focus and event behavior stay the same. Product
button styling belongs in application components, not this portable core. The
Crabit settings sample uses its own ActionButton and only DisplayConfApp has been
migrated to object callbacks; other applications are not bulk rewritten.

Normal navigation limitations remain separately characterized. This work does
not add thread safety, exhaustive allocation failure recovery or device/RTOS
implementations.

### Terminal cleanup

When the platform's UI loop has stopped, call `AppManager::shutdown()` before
destroying its Canvas, InputDriver, allocator or allocation observers. This
releases all foreground Apps (including the root), background Apps, render trees,
retained route configuration, borrowed popup references, owned toast bytes and
manager container capacity. Render borrowers are removed before App destruction.
FRAME Apps receive `onUnmount()` before `onDestroy()`; shutdown does not rebuild
ancestors, invoke Back or resume suspended Apps. It is idempotent. Reuse starts
with `AppManager::init()` while the platform dependencies are alive.

`WindowManager::clear()` releases its views/configuration and back hook, not
borrowed Applications or platform adapters. Its destructor also clears views
and unbinds `instance` if it still points to that manager. Standalone consumers
must declare borrowed Apps/adapters before the manager (or explicitly clear
while they are alive). AppManager still owns Apps passed to `launch()`; a
WindowManager destructor alone does not shut them down.

Terminal cleanup is synchronous and UI-thread-only. Destruction callbacks cannot
start new UI work during cleanup: `launch` and `push` reject and delete newly
transferred owned objects without starting them. `Popup::show` rejects a borrowed
configuration without deleting it. Other navigation
and popup updates are suppressed. This small transient guard is not a navigation
queue or a general callback lifetime solution. `Popup::reset()` is a terminal helper with
the precondition that popup RenderObjects have already been removed.

Windows uses this cleanup before releasing SDL resources. No watch power/sleep,
boot, flashing or radio path calls it. Ordinary raw-route ownership remains separate. Terminal cleanup
does not recover a caller-owned configuration abandoned without its own owner.

### Popup and Toast ownership correction — 2026-10-21

`Popup::show(Widget*, w, h)` borrows its configuration. `show`, `hide`, `reset` and
shutdown rejection never delete that Widget. This corrects the earlier mismatch
where ordinary show/hide borrowed but terminal reset/rejection deleted. Existing
owners such as CompanionPairPrompt keep their unique_ptr. A direct `show(new ...)`
without an external owner is not an ownership transfer and leaks configuration.
No new owning API is introduced in this correction.

Configuration, text, state and callback receivers must outlive their last popup
RenderObject. Hide/replacement schedules layout; returning from it is **not** a
release barrier. Keep the owner alive through the next completed layout that
removes its render, or clear the WindowManager before resetting/destroying owners.
Standalone consumers must call `Popup::reset()` after their render teardown;
WindowManager::clear() alone does not clear this global controller's references.

Each show/hide advances a revision, including same-address hide/show before a
draw. RenderPopupRoot recreates stale renders and excludes them from painting,
event dispatch and focus traversal. WindowManager refreshes layout/focus before
dispatching non-Back input. It never dereferences old focus-cache addresses to
clear them; hidden background focus is cleared through the live render subtree.
An invalidation raised by a factory/layout is retained for the next frame, with
no same-frame retry loop. This does not make arbitrary navigation, shutdown,
cached Render pointer use or AppManager reentry safe inside callbacks/layout.
Back still follows existing system/navigation behavior, not automatic dismiss.

`Popup::showToast` copies all NUL-terminated message bytes into a dynamically
allocated buffer through the existing MemoryPool adapter. Temporary strings and
self-alias replacement are safe; no 32-byte StaticString limit is applied. Expiry,
reset, a null/empty message or nonpositive duration release the copy. Replacement
copies before freeing the old buffer, so both can exist briefly. A null-returning
adapter failure preserves the old Toast; dynamic storage without an adapter uses
the existing throwing operator-new fallback, not a universal no-throw OOM policy.
No allocation adapter may change while its allocations remain live.

Toast uses the `UI_Text` allocation tag. Existing Widget/Render/Vector usage and
peak counters do not count that category. Leak evidence must include adapter or
global allocation accounting, not just those three usage counters. Popup tests
track actual adapter blocks in both storage profiles.


### AppManager callback boundaries — 2026-10-21

WITHDRAWN after user-reported BW16 radio App regression. AppManager and runtime
have been restored to 6de3ac4; the following callback contract describes the
rejected candidate, NOT the current implementation. Do not rely on deferred
exit/shutdown or callback reentry protection in the restored version.

The UI thread remains the only caller. AppManager wraps its managed App callbacks;
exit and shutdown requested there take effect after the callback returns, rather
than deleting its receiver mid-call. A foreground transition suppresses the old
App's render in that tick. Nested tick/event dispatch is ignored. Background
services added during a tick first run on the next tick; shutdown stops iteration.
There is no new scheduler, command queue or per-App allocation.

Normal exit preserves onDestroy -> cooperative route pop/parent rebuild -> App
destructor order. Launch during teardown is rejected and its transferred App is
deleted; callbacks must not assume such a launch succeeded. EXCLUSIVE self-exit
does not close the parent a second time. This boundary does not protect arbitrary
WindowManager operations or a render/layout callback that is not an App callback.
It is not a cross-thread lifetime mechanism or a pool-capacity/OOM redesign.

## Related contracts

- [Library introduction](../README.md)
- Layout semantics
- Tree and navigation
- Lifecycle
- Callback components validation
- Terminal shutdown validation
