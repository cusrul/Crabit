#ifndef CRABIT_ARDUBOY_COMPAT_APP_H
#define CRABIT_ARDUBOY_COMPAT_APP_H

#include "Arduboy2Compat.h"
#include "ArduboyPortRuntime.h"
#include "../../../AmebaStore.h"

#include <new>
#include <type_traits>

// Frame-driven NanoUI application for the compatibility layer. It owns input
// translation, lifecycle, optional tilt, the exit gesture, and (for the
// on-demand catalogue ports) the typed port State created before launch.
//
// PAT action policy: STM32 already resolves raw bit 4 into one single tap and
// raw bit 5 into one completed double tap (its 220 ms window lives there and
// is not re-implemented here). This adapter only decides which Arduboy button
// each resolved tap stands for: by default a single tap is the primary A
// button and a double tap is the secondary B button. A port that uses B as its
// frequent confirm/fire key may swap the pair, but it must not add a third
// meaning to a tap or make both taps the same button.
//
// The legacy six profile keeps its existing static engines: those Apps never
// override prepareAdmission(), and their constructors keep binding port_ptr to
// the accepted static sandbox objects. Catalogue ports instead implement
// prepareAdmission() and are created through CrabitArduboy::tryCreate<T>().
class ArduboyCompatApp : public Application {
protected:
    Arduboy2Compat *port_ptr = nullptr;
    Canvas *canvas_ptr = nullptr;
    InputDriver *input_ptr = nullptr;
    bool tiltEnabled = true;

    virtual void game_setup() = 0;
    virtual void game_loop() = 0;

public:
    ~ArduboyCompatApp() override {
        // Lifecycle fallback: a destroyed App can never leave a playing voice
        // or a score pointer behind for the next owner.
        CrabitArduboyAudio::end(this);
        releasePortState();
    }

    AppMode getMode() override { return APP_MODE_FRAME; }

    // Tilt is the primary d-pad on this device: the side buttons are not
    // usable while holding the watch like a gamepad.
    void setTiltEnabled(bool enabled) { tiltEnabled = enabled; }

    // Runs before AppManager::launch. AppManager suspends the previous FRAME
    // App before onMount and onMount returns void, so every allocation and the
    // one-live-instance check must finish here; a false result leaves the
    // current App untouched and the caller must not launch anything.
    bool prepareForLaunch() { return prepareAdmission(); }

    void onMount(Canvas *canvas, InputDriver *input) override {
        canvas_ptr = canvas;
        input_ptr = input;
        suppressed = AmebaStore::raw_btn_mask; // Menu launch keys must be released.
        primaryPulse = secondaryPulse = 0;
        tiltX = tiltY = 0;
        // Fresh owner: a previous App's voice is stopped and the relaunch
        // default is "sound on" until this App's menu says otherwise. This is
        // deliberately not read from EEPROM[2]: ports use that byte (or none)
        // for their own data, and zero-save ports guard every EEPROM access.
        CrabitArduboyAudio::begin(this, true);
        audioEnabledSnapshot = true;
        if (!port_ptr || !canvas_ptr) {
            requestExit();
            return;
        }
        port_ptr->resetRuntime();
        port_ptr->attachCanvas(canvas_ptr);
        game_setup();
    }

    void onUpdate(uint32_t now_ms) override {
        if (!port_ptr || !canvas_ptr) {
            requestExit();
            return;
        }

        // Drain a bounded number of UI events. Games use the raw mask; leaving
        // one event per call prevents a noisy producer from starving services.
        if (input_ptr) {
            for (unsigned i = 0; i < 16; ++i)
                if (input_ptr->poll().type == EVT_NONE) break;
        }

        const uint8_t raw = AmebaStore::raw_btn_mask;

        // PAT hold >= 800 ms: exit. The STM32 owns the timing so the gesture
        // works the same in every port and no side button is needed.
        if (raw & (1 << 6)) {
            requestExit();
            return;
        }

        suppressed &= raw;
        const uint8_t fresh = raw & ~suppressed;

        // One resolved PAT tap becomes one short press of this port's primary
        // (single tap) or secondary (double tap) button. The 20 ms telemetry
        // pulse is stretched to a few UI frames so both pressed() and
        // justPressed() see the same edge; the tap/double-tap timing itself
        // stays in STM32 and is unchanged.
        if (fresh & (1 << 4)) primaryPulse = kActionPulseFrames;
        if (fresh & (1 << 5)) secondaryPulse = kActionPulseFrames;

        uint8_t buttons = 0;
        if (primaryPulse) {
            buttons |= primaryActionButton();
            --primaryPulse;
        }
        if (secondaryPulse) {
            buttons |= secondaryActionButton();
            --secondaryPulse;
        }

        if (tiltEnabled) {
            const float ax = AmebaStore::imu_ax;
            const float ay = AmebaStore::imu_ay;
            const float az = AmebaStore::imu_az;
            if (az > 0.3f) {
                // Enter at 0.20 g, release at 0.12 g: a single threshold
                // chatters when the watch is held near the edge.
                const float enter = 0.20f, release = 0.12f;
                if (tiltX == 0) tiltX = ax > enter ? 1 : (ax < -enter ? -1 : 0);
                else if (tiltX * ax < release) tiltX = 0;
                if (tiltY == 0) tiltY = ay > enter ? 1 : (ay < -enter ? -1 : 0);
                else if (tiltY * ay < release) tiltY = 0;
            } else {
                tiltX = tiltY = 0;
            }
            if (tiltX > 0) buttons |= RIGHT_BUTTON;
            else if (tiltX < 0) buttons |= LEFT_BUTTON;
            if (tiltY > 0) buttons |= DOWN_BUTTON;
            else if (tiltY < 0) buttons |= UP_BUTTON;
        }

        port_ptr->updateHwButton(buttons);
        game_loop();

        // One bounded audio step on the existing foreground frame tick. Game
        // code only registered intent above; serial output happens here.
        CrabitArduboyAudio::tick(now_ms);
    }

    // AppManager suspends a FRAME App when another one is pushed. Stop audio
    // and drop ownership so a suspended background App cannot emit on onTick().
    // The current per-App choice is saved here, not globally: another App may
    // mount and change the shared flag while this one is suspended.
    void onSuspend() override {
        audioEnabledSnapshot = CrabitArduboyAudio::enabled();
        CrabitArduboyAudio::end(this);
    }

    void onResume() override {
        // Resume restores this App's own choice; no stale score or note is
        // resumed, because the old pointer was dropped at suspend.
        CrabitArduboyAudio::begin(this, false);
        CrabitArduboyAudio::setEnabled(audioEnabledSnapshot);
    }

    void onUnmount() override {
        // Silence before the port State and its score pointers are released.
        CrabitArduboyAudio::end(this);
        if (port_ptr) {
            port_ptr->resetRuntime();
            port_ptr->attachCanvas(nullptr);
        }
        if (canvas_ptr) {
            canvas_ptr->clear();
            canvas_ptr->display();
        }
        canvas_ptr = nullptr;
        input_ptr = nullptr;
        primaryPulse = secondaryPulse = 0;
        tiltX = tiltY = 0;
        suppressed = 0;
    }

protected:
    // Per-port PAT action policy. Default: single tap = A (primary), double
    // tap = B (secondary). Swap the pair for ports whose frequent key is B.
    // Both must stay distinct nonzero button masks.
    virtual uint8_t primaryActionButton() const { return A_BUTTON; }
    virtual uint8_t secondaryActionButton() const { return B_BUTTON; }

    // Legacy/static ports need no admission work; their constructor already
    // bound port_ptr to the accepted sandbox object.
    virtual bool prepareAdmission() { return true; }

    // Hand the created State to the Application. `unbind` clears the per-port
    // stable handle and module pointers before the State is freed; `destroy`
    // then runs the typed destructor and releases the allocation exactly once.
    void adoptPortState(void *state, void (*destroy)(uint8_t, void *), void (*unbind)(),
                        uint8_t port_id, uint16_t save_bytes, Arduboy2Compat *engine) {
        port_state_ = state;
        port_state_destroy_ = destroy;
        port_state_unbind_ = unbind;
        port_id_ = port_id;
        port_save_bytes_ = save_bytes;
        port_ptr = engine;
    }

private:
    void *port_state_ = nullptr;
    void (*port_state_destroy_)(uint8_t, void *) = nullptr;
    uint8_t port_id_ = 0;
    uint16_t port_save_bytes_ = 0;
    void (*port_state_unbind_)() = nullptr;

    void releasePortState() {
        if (!port_state_) return;
        // Persist the logical 1,024-byte RAM EEPROM to the retained session
        // buffer while the engine is still alive. Suspended Apps are not
        // released here; they keep their State until AppManager destroys them.
        if (port_save_bytes_ && port_ptr)
            CrabitArduboy::commitSaveSlot(port_id_, port_ptr->eeprom);
        // Stop navigation before readers/keys can be used again.
        if (port_state_unbind_) port_state_unbind_();
        if (port_state_destroy_) port_state_destroy_(port_id_, port_state_);
        port_state_ = nullptr;
        port_state_destroy_ = nullptr;
        port_state_unbind_ = nullptr;
        port_ptr = nullptr;
    }

    uint8_t suppressed = 0;
    static constexpr uint8_t kActionPulseFrames = 3;
    uint8_t primaryPulse = 0, secondaryPulse = 0;
    int8_t tiltX = 0, tiltY = 0;
    bool audioEnabledSnapshot = true;
};

namespace CrabitArduboy {

namespace detail {
template <class State>
void destroyTypedPortState(uint8_t port_id, void *state) {
    destroyPortState(port_id, static_cast<State *>(state));
}
} // namespace detail

// Production entry point for catalogue ports. The registry/menu must call this
// and launch the result only when it is non-null:
//
//   if (GalaxionApp *app = CrabitArduboy::tryCreate<GalaxionApp>())
//       AppManager::launch(app);
//
// Everything the port needs (typed State with its engine/framebuffer, save
// session slot, one-live-instance claim) is prepared here. On failure nothing
// was launched and the current App is unchanged.
// This handles allocators that return nullptr. The stock BW16 SDK halts on
// actual heap exhaustion before returning; see ArduboyPortRuntime.h. Admission
// refusal (e.g. duplicate port id) is recoverable, but target OOM is not proven.
template <class App>
App *tryCreate() {
    static_assert(std::is_base_of<ArduboyCompatApp, App>::value,
                  "tryCreate<T> requires an ArduboyCompatApp subclass");
    App *app = new (std::nothrow) App();
    if (!app) return nullptr;
    if (!app->prepareForLaunch()) {
        delete app;
        return nullptr;
    }
    return app;
}

} // namespace CrabitArduboy

#endif
