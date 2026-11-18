// Host-only checks for the shared PAT action policy and the live port-id
// slots. No game code: the fixture isolates ArduboyCompatApp + its runtime so a
// policy or slot regression is not hidden inside a game's own test.
#include "app/compat/arduboy/ArduboyCompatApp.h"
#include <assert.h>
#include <iostream>
#include <limits.h>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
} // namespace AmebaStore

// Hopper (the full upstream port) reserves id 21, so the shared runtime must
// expose 22 slots and reject id 22.
static_assert(CrabitArduboy::kPortIdCount == 22, "Hopper id 21 needs a 22nd slot");

namespace {

struct TinyState {
    uint8_t value;
};

void portIdBounds() {
    TinyState *state = CrabitArduboy::createPortState<TinyState>(21);
    assert(state && "id 21 (Hopper) must be admitted");
    assert(state->value == 0);
    assert(CrabitArduboy::createPortState<TinyState>(21) == nullptr); // duplicate
    assert(CrabitArduboy::createPortState<TinyState>(22) == nullptr); // out of range
    Arduboy2Compat::RamEeprom eeprom;
    assert(CrabitArduboy::attachSaveSlot(21, 1024, eeprom));
    assert(!CrabitArduboy::attachSaveSlot(22, 1024, eeprom));
    assert(!CrabitArduboy::attachSaveSlot(21, 2048, eeprom));
    CrabitArduboy::destroyPortState(21, state);
    // The slot is reusable once the live owner released it.
    TinyState *again = CrabitArduboy::createPortState<TinyState>(21);
    assert(again);
    CrabitArduboy::destroyPortState(21, again);
    CrabitArduboy::releaseRetainedSaves();
    assert(CrabitArduboy::allocationStats().live == 0);
}

// The adapter must never synthesise a fire button: a battle-looking app with
// no PAT input and no tilt must reach the engine with an empty mask.
class PolicyApp : public ArduboyCompatApp {
public:
    Arduboy2Compat engine;
    uint8_t primary = A_BUTTON;
    uint8_t secondary = B_BUTTON;
    unsigned setups = 0;

    PolicyApp() { port_ptr = &engine; }
    const char *getAppName() override { return "policy"; }

protected:
    uint8_t primaryActionButton() const override { return primary; }
    uint8_t secondaryActionButton() const override { return secondary; }
    void game_setup() override {
        ++setups;
        engine.beginNoLogo();
        engine.setFrameRate(60);
    }
    void game_loop() override {
        if (!engine.nextFrame()) return;
        engine.pollButtons();
        engine.clear();
        engine.display();
    }
};

void step(Application &app, uint8_t raw) {
    AmebaStore::raw_btn_mask = raw;
    fakeMillis += 17;
    app.onUpdate(fakeMillis);
}

void policy() {
    Canvas canvas;
    InputDriver input;

    PolicyApp app;
    app.onMount(&canvas, &input);
    assert(app.setups == 1);

    // Default policy: single PAT tap = A for exactly three UI frames.
    step(app, 1U << 4);
    assert(app.engine.pressed(A_BUTTON));
    assert(app.engine.justPressed(A_BUTTON));
    assert(!app.engine.pressed(B_BUTTON));
    step(app, 0);
    assert(app.engine.pressed(A_BUTTON) && !app.engine.justPressed(A_BUTTON));
    step(app, 0);
    assert(app.engine.pressed(A_BUTTON));
    step(app, 0);
    assert(!app.engine.pressed(A_BUTTON));
    assert(app.engine.justReleased(A_BUTTON));

    // Default policy: double PAT tap = B, never A.
    step(app, 1U << 5);
    assert(app.engine.pressed(B_BUTTON) && !app.engine.pressed(A_BUTTON));
    for (int i = 0; i < 4; ++i) step(app, 0);
    assert(!app.engine.pressed(B_BUTTON));

    // No input, no synthetic A/B (auto fire belongs to each game's fire site).
    for (int i = 0; i < 120; ++i) {
        step(app, 0);
        assert(app.engine.notPressed(A_BUTTON | B_BUTTON));
    }

    // Tilt still supplies the d-pad and does not touch the action buttons.
    AmebaStore::imu_ax = 0.5f;
    step(app, 0);
    assert(app.engine.pressed(RIGHT_BUTTON));
    assert(app.engine.notPressed(A_BUTTON | B_BUTTON));
    AmebaStore::imu_ax = 0.0f;
    AmebaStore::imu_ay = 0.5f;
    step(app, 0);
    step(app, 0);
    assert(app.engine.pressed(DOWN_BUTTON));
    AmebaStore::imu_ay = 0.0f;
    step(app, 0);
    step(app, 0);
    assert(app.engine.notPressed(UP_BUTTON | DOWN_BUTTON | LEFT_BUTTON | RIGHT_BUTTON));

    // Leaving with a pulse still pending must not resume that pulse.
    step(app, 1U << 4);
    assert(app.engine.pressed(A_BUTTON));
    app.onUnmount();
    assert(!app.engine.buttonsState());
    app.onMount(&canvas, &input);
    step(app, 0);
    assert(app.engine.notPressed(A_BUTTON | B_BUTTON));

    // The launch key that opened the App is suppressed until released.
    app.onUnmount();
    AmebaStore::raw_btn_mask = 1U << 5;
    app.onMount(&canvas, &input);
    step(app, 1U << 5);
    assert(app.engine.notPressed(A_BUTTON | B_BUTTON));
    step(app, 0);
    step(app, 0);
    step(app, 1U << 5);
    assert(app.engine.pressed(B_BUTTON));
    app.onUnmount();

    // Swapped policy (e.g. ARDUBULLET: B is the frequent key): single tap = B,
    // double tap = A, and neither fires the other button.
    PolicyApp swapped;
    swapped.primary = B_BUTTON;
    swapped.secondary = A_BUTTON;
    swapped.onMount(&canvas, &input);
    step(swapped, 1U << 4);
    assert(swapped.engine.pressed(B_BUTTON));
    assert(!swapped.engine.pressed(A_BUTTON));
    for (int i = 0; i < 4; ++i) step(swapped, 0);
    assert(swapped.engine.notPressed(A_BUTTON | B_BUTTON));
    step(swapped, 1U << 5);
    assert(swapped.engine.pressed(A_BUTTON));
    assert(!swapped.engine.pressed(B_BUTTON));
    swapped.onUnmount();

    // PAT hold is the shared exit gesture in both policies; no action fires.
    PolicyApp hold;
    hold.onMount(&canvas, &input);
    fakeMillis += 17;
    AmebaStore::raw_btn_mask = 1U << 6;
    hold.onUpdate(fakeMillis);
    assert(hold.exitRequested());
    assert(hold.engine.notPressed(A_BUTTON | B_BUTTON));
    hold.onUnmount();

    // A missing port engine fails closed instead of faking a game.
    PolicyApp broken;
    broken.onMount(nullptr, nullptr);
    assert(broken.exitRequested());
}

} // namespace

int main() {
    portIdBounds();
    policy();
    std::cout << "action policy runtime passed\n";
    return 0;
}
