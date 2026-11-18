#pragma once
// Included after the game's app.h: these helpers use the real adapter surface.

// The launcher macros were restored by the port's push/pop guard. Drop them so
// the fixtures can name the ports' own STATE_* enumerators.
#undef STATE_TITLE
#undef STATE_PLAY
#undef STATE_GAME_OVER
#undef STATE_HELP

// STM32 already resolved the PAT gesture: raw bit 4 is one single tap, raw bit
// 5 is one completed double tap. The adapter only maps each to a button.
static const uint8_t PAT_SINGLE = 1U << 4;
static const uint8_t PAT_DOUBLE = 1U << 5;

static float g_ax = 0.0f, g_ay = 0.0f;
static void tilt(float ax, float ay) { g_ax = ax; g_ay = ay; }
static void level() { g_ax = g_ay = 0.0f; }

static void frames(Application &app, unsigned count, uint8_t raw = 0) {
    for (unsigned i = 0; i < count; ++i) {
        AmebaStore::raw_btn_mask = raw;
        AmebaStore::imu_ax = g_ax;
        AmebaStore::imu_ay = g_ay;
        AmebaStore::imu_az = 1.0f;
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
        assert(!app.exitRequested());
    }
}

// One resolved PAT tap. The adapter stretches the 20 ms telemetry pulse to
// three UI frames, so a neutral tail lets it finish before the next tap.
static void tap(Application &app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 5);
}

static void tiltTap(Application &app, float ax, float ay) {
    tilt(ax, ay);
    frames(app, 1);
    level();
    frames(app, 3);
}

// Catalogue Apps own an on-demand port State; only CrabitArduboy::tryCreate
// prepares the engine/framebuffer, the retained save slot and the single-live
// claim. A raw stack object would leave every sandbox pointer null.
template <class App>
static App &requireApp() {
    App *app = CrabitArduboy::tryCreate<App>();
    assert(app && "tryCreate must prepare the port resources");
    return *app;
}
