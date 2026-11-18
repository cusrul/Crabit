// Host-only runtime for the actual ARDUBULLETs port. Only the device boundary
// (canvas, input telemetry, millis) is faked; gameplay code is unmodified.
// Real NanoUI geometry must be visible before the compat header: this is the
// include order that exposed the former global Point/Rect collision.
#include "NanoUI/compatibility/geometry.h"
#include "app/apps/games/ardubullets/app.h"
#include <assert.h>
#include <type_traits>
#include <stdio.h>
#include <string.h>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
}

static bool bufNonEmpty(const Canvas &canvas) {
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x)
            if (canvas.pixels[y][x]) return true;
    return false;
}

static void savePbm(const Canvas &canvas, const char *dir, const char *name) {
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pbm", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P1\n128 64\n");
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; ++x) fputc(canvas.pixels[y][x] ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}

static void frame(ArdubulletsApp &app, Canvas &canvas, int count, const char *dir = nullptr,
                  const char *capture = nullptr) {
    for (int i = 0; i < count; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
        if (dir && capture && i == count / 2) savePbm(canvas, dir, capture);
    }
}

// One tilt edge: the adapter needs a neutral frame to re-arm the direction.
static void tiltTap(ArdubulletsApp &app, Canvas &canvas, float ax, float ay) {
    AmebaStore::imu_ax = ax;
    AmebaStore::imu_ay = ay;
    frame(app, canvas, 1);
    AmebaStore::imu_ax = AmebaStore::imu_ay = 0;
    frame(app, canvas, 2);
}

static int countLiveShots() {
    int count = 0;
    for (const auto &shot : ArdubulletsSandbox::Game::portState().shots)
        if (shot.x < WIDTH) ++count;
    return count;
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

int main(int argc, char **argv) {
    // Real NanoUI Point/Rect stay untouched; the Arduboy collision types are
    // namespaced and only this port aliases them inside its sandbox.
    static_assert(!std::is_same<Point, CrabitArduboy::Point>::value,
                  "Arduboy geometry must not alias NanoUI's global Point");
    static_assert(!std::is_same<Rect, CrabitArduboy::Rect>::value,
                  "Arduboy geometry must not alias NanoUI's global Rect");
    Point nanoPoint{2, 3};
    Rect nanoRect{0, 0, 4, 4};
    assert(nanoPoint.x == 2 && nanoRect.width() == 4);
    assert((CrabitArduboy::Point(3, 3).x == 3));
    assert(Arduboy2Compat::collide(CrabitArduboy::Point(3, 3), CrabitArduboy::Rect(2, 2, 2, 2)));
    assert(!Arduboy2Compat::collide(CrabitArduboy::Point(4, 3), CrabitArduboy::Rect(2, 2, 2, 2)));
    assert(Arduboy2Compat::collide(CrabitArduboy::Rect(0, 0, 4, 4), CrabitArduboy::Rect(3, 3, 4, 4)));
    assert(bitRead(0x8, 3) == 1 && constrain(7, 0, 5) == 5 && PI > 3.14 && PI < 3.15);
    uint8_t toggled = 0x10;
    CrabitArduboy::toggleBit(toggled, 4);
    assert(toggled == 0 && CrabitArduboy::readBit(0x8, 3));
    const char *dir = argc > 1 ? argv[1] : nullptr;
    Canvas canvas;
    InputDriver input;

    ArdubulletsApp &app = requireApp<ArdubulletsApp>();
    // ARDUBULLET's policy is B-single/A-double; auto fire defaults ON but the
    // manual paths below are what this section verifies, so switch it off
    // explicitly (the dedicated section further down tests it).
    assert(ArdubulletsSandbox::autoFirePreference());
    ArdubulletsSandbox::autoFirePreference() = false;
    app.onMount(&canvas, &input);
    frame(app, canvas, 130, dir, "ardubullets-logo-title");
    assert(ArdubulletsSandbox::portState().mode.mode == ArdubulletsSandbox::MODE_TITLE);
    assert(bufNonEmpty(canvas));

    // Deterministic seed and a real menu confirmation (B is the upstream
    // confirm key; ARDUBULLET maps it to the primary single PAT tap).
    ArdubulletsSandbox::portState().common.record.gameSeed = 12345;
    AmebaStore::raw_btn_mask = 1 << 4;
    frame(app, canvas, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, canvas, 2);
    assert(ArdubulletsSandbox::portState().mode.mode == ArdubulletsSandbox::MODE_GAME);

    // Start animation, then steer right while firing through the real engine.
    frame(app, canvas, 70, dir, "ardubullets-start");
    int16_t beforeX = ArdubulletsSandbox::Game::portState().playerX;
    AmebaStore::imu_ax = 0.6f;
    AmebaStore::raw_btn_mask = 1 << 4;
    frame(app, canvas, 90, dir, "ardubullets-playing");
    assert(ArdubulletsSandbox::Game::portState().state == ArdubulletsSandbox::Game::STATE_PLAYING);
    assert(ArdubulletsSandbox::Game::portState().playerX != beforeX);
    int liveShots = 0;
    for (const auto &shot : ArdubulletsSandbox::Game::portState().shots)
        if (shot.x < WIDTH) ++liveShots;
    assert(liveShots > 0);
    assert(ArdubulletsSandbox::Game::portState().gameFrames > 0);
    assert(bufNonEmpty(canvas));
    // Enemy groups appear on the scripted five-second boundaries.
    frame(app, canvas, 320);
    assert(ArdubulletsSandbox::Game::portState().gameFrames >= 300);

    // The pause key is now the secondary double PAT tap; B still confirms.
    AmebaStore::raw_btn_mask = 1 << 5;
    frame(app, canvas, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, canvas, 2);
    assert(ArdubulletsSandbox::Game::portState().isMenu);
    AmebaStore::raw_btn_mask = 1 << 4;
    frame(app, canvas, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, canvas, 2);
    assert(!ArdubulletsSandbox::Game::portState().isMenu);

    // A second launch of the same port id is refused while this one is live:
    // the State, engine and save slot stay with the running Application.
    assert(CrabitArduboy::tryCreate<ArdubulletsApp>() == nullptr);

    // Release input, mount again: all modal state must restart from logo while
    // the RAM record survives.
    AmebaStore::imu_ax = 0;
    AmebaStore::raw_btn_mask = 0;
    app.onUnmount();
    assert(!bufNonEmpty(canvas));
    app.onMount(&canvas, &input);
    assert(ArdubulletsSandbox::portState().mode.mode == ArdubulletsSandbox::MODE_LOGO);
    assert(ArdubulletsSandbox::Title::portState().state == ArdubulletsSandbox::Title::STATE_INIT);
    assert(ArdubulletsSandbox::Game::portState().isMenu == false);
    assert(ArdubulletsSandbox::Game::portState().pShotCursor >= ArdubulletsSandbox::Game::portState().shots);
    assert(ArdubulletsSandbox::Game::portState().pShotCursor <
           ArdubulletsSandbox::Game::portState().shots +
               sizeof(ArdubulletsSandbox::Game::portState().shots) / sizeof(ArdubulletsSandbox::Game::portState().shots[0]));
    // Bounded idle loop, including the title screen and its menu.
    frame(app, canvas, 2000);
    assert(ArdubulletsSandbox::portState().mode.mode == ArdubulletsSandbox::MODE_TITLE);
    // The started run wrote its record into RAM EEPROM, so the fresh title must
    // load that stored record (one play) instead of inheriting live game state.
    assert(ArdubulletsSandbox::portState().common.recordState == ArdubulletsSandbox::RECORD_STORED);
    assert(ArdubulletsSandbox::portState().common.record.playCount == 1);
    assert(ArdubulletsSandbox::portState().common.record.gameSeed <= 14348907UL); // GAME_SEED_MAX
    assert(!ArdubulletsSandbox::portState().common.isRecordDirty);

    // PAT hold is owned by the shared adapter: bit 6 requests exit. Checked
    // last because an exit request is terminal for that Application.
    AmebaStore::raw_btn_mask = 1 << 6;
    app.onUpdate(fakeMillis + 17);
    assert(app.exitRequested());
    app.onUnmount();
    AmebaStore::raw_btn_mask = 0;
    delete &app;

    // ---- Auto fire: default ON, battle-only, menu/game-over safe ---------
    {
        ArdubulletsSandbox::autoFirePreference() = true;
        ArdubulletsApp &autoApp = requireApp<ArdubulletsApp>();
        autoApp.onMount(&canvas, &input);
        frame(autoApp, canvas, 140);
        ArdubulletsSandbox::State &s = ArdubulletsSandbox::portState();
        assert(s.mode.mode == ArdubulletsSandbox::MODE_TITLE);
        // A fresh typed State has zeroed shot slots; initShots() marks idle
        // shots by moving them past the right edge. Normalise to that marker so
        // "no shot appeared" is observable before the first game starts.
        for (auto &shot : s.game.shots) shot.x = WIDTH;
        // The title screen has no battle: a long idle run must not fire.
        frame(autoApp, canvas, 120);
        assert(s.game.playerFire == 0);
        assert(countLiveShots() == 0);

        s.common.record.gameSeed = 12345;
        AmebaStore::raw_btn_mask = 1 << 4; // B single tap confirms NEW BATTLE
        frame(autoApp, canvas, 2);
        AmebaStore::raw_btn_mask = 0;
        frame(autoApp, canvas, 2);
        assert(s.mode.mode == ArdubulletsSandbox::MODE_GAME);
        // The start animation is not a battle: auto fire must wait for the
        // real STATE_PLAYING transition.
        frame(autoApp, canvas, 60);
        assert(s.game.state == ArdubulletsSandbox::Game::STATE_START);
        assert(s.game.playerFire == 0);
        assert(countLiveShots() == 0);
        frame(autoApp, canvas, 80);
        assert(s.game.state == ArdubulletsSandbox::Game::STATE_PLAYING);

        // Default ON: shots appear with no PAT input, and the slow flag is
        // untouched because auto fire never holds B.
        frame(autoApp, canvas, 40);
        assert(countLiveShots() > 0);
        assert(s.game.playerSlow == 0);
        assert(!s.game.isMenu);

        // Pause menu: no stray fire while the menu owns the frame.
        AmebaStore::raw_btn_mask = 1 << 5; // A double tap opens the menu
        frame(autoApp, canvas, 2);
        AmebaStore::raw_btn_mask = 0;
        frame(autoApp, canvas, 2);
        assert(s.game.isMenu);
        frame(autoApp, canvas, 1, dir, "ardubullets-autofire-menu");
        decltype(s.game.shots) shotsBefore;
        memcpy(&shotsBefore, &s.game.shots, sizeof(shotsBefore));
        const uint16_t gameFramesBefore = s.game.gameFrames;
        frame(autoApp, canvas, 60);
        assert(memcmp(&shotsBefore, &s.game.shots, sizeof(shotsBefore)) == 0);
        assert(s.game.gameFrames == gameFramesBefore);

        // The pause menu carries the toggle: AUTO FIRE is the last row.
        tiltTap(autoApp, canvas, 0, 0.5f);
        tiltTap(autoApp, canvas, 0, 0.5f);
        tiltTap(autoApp, canvas, 0, 0.5f);
        assert(ArdubulletsSandbox::getMenuItemPos() == 3);
        AmebaStore::raw_btn_mask = 1 << 4; // B confirms/activates the row
        frame(autoApp, canvas, 2);
        AmebaStore::raw_btn_mask = 0;
        frame(autoApp, canvas, 2);
        assert(!ArdubulletsSandbox::autoFirePreference());
        assert(s.game.isMenu);
        assert(ArdubulletsSandbox::getMenuItemPos() == 3);

        // Back to CONTINUE and resume: off means manual fire only.
        tiltTap(autoApp, canvas, 0, -0.5f);
        tiltTap(autoApp, canvas, 0, -0.5f);
        tiltTap(autoApp, canvas, 0, -0.5f);
        assert(ArdubulletsSandbox::getMenuItemPos() == 0);
        AmebaStore::raw_btn_mask = 1 << 4;
        frame(autoApp, canvas, 2);
        AmebaStore::raw_btn_mask = 0;
        frame(autoApp, canvas, 2);
        assert(!s.game.isMenu);
        for (auto &shot : s.game.shots) shot.x = WIDTH;
        s.game.playerFire = 0;
        frame(autoApp, canvas, 40);
        assert(countLiveShots() == 0); // auto fire off: nothing fires on its own
        AmebaStore::raw_btn_mask = 1 << 4; // manual B still fires a burst
        frame(autoApp, canvas, 2);
        AmebaStore::raw_btn_mask = 0;
        frame(autoApp, canvas, 20);
        assert(countLiveShots() > 0);

        // Game over is not a battle either.
        ArdubulletsSandbox::autoFirePreference() = true;
        for (auto &shot : s.game.shots) shot.x = WIDTH;
        s.game.state = ArdubulletsSandbox::Game::STATE_OVER;
        s.game.isDefeated = true;
        s.game.playerFire = 0;
        s.common.counter = 0;
        frame(autoApp, canvas, 60);
        assert(countLiveShots() == 0);

        // Lifecycle: the preference is a powered-session value and survives
        // re-entry, while every transient fire field is reset by the port.
        ArdubulletsSandbox::autoFirePreference() = false;
        autoApp.onUnmount();
        autoApp.onMount(&canvas, &input);
        assert(!ArdubulletsSandbox::autoFirePreference());
        assert(ArdubulletsSandbox::portState().mode.mode == ArdubulletsSandbox::MODE_LOGO);
        assert(ArdubulletsSandbox::Game::portState().playerFire == 0);
        for (auto &shot : ArdubulletsSandbox::Game::portState().shots) shot.x = WIDTH;
        assert(countLiveShots() == 0);
        frame(autoApp, canvas, 200);
        assert(ArdubulletsSandbox::portState().mode.mode == ArdubulletsSandbox::MODE_TITLE);
        assert(countLiveShots() == 0);
        autoApp.onUnmount();
        delete &autoApp;
        ArdubulletsSandbox::autoFirePreference() = true;
    }

    // ---- AVR-compatible save image at 1,008..1,023 -----------------------
    // The upstream record is 10 bytes: avr-gcc does not pad the trailing
    // uint16_t, so the image is signature (4) + record (10) + checksum (2).
    // A padded 32-bit struct would store 12 bytes and push the checksum past
    // 1,023, where RamEeprom silently discards it.
    {
        ArdubulletsApp &save = requireApp<ArdubulletsApp>();
        save.onMount(&canvas, &input);
        frame(save, canvas, 140);
        assert(ArdubulletsSandbox::portState().mode.mode == ArdubulletsSandbox::MODE_TITLE);

        ArdubulletsSandbox::State &state = ArdubulletsSandbox::portState();
        ArdubulletsSandbox::clearRecord(); // deterministic 16-byte window
        state.common.record.gameSeed = 0x00A1B2C3;
        state.common.record.rank = 9;
        state.common.record.isCodeManual = true;
        state.common.record.isBlinkLED = false;
        state.common.record.isDrawEdge = true;
        state.common.record.isCleared = false;
        state.common.record.playFrames = 0x11223344;
        state.common.record.playCount = 0xBEEF;
        state.common.isRecordDirty = true;
        ArdubulletsSandbox::writeRecord();

        // Golden image for every field, written by the port's real writer.
        const uint8_t expected[16] = {
            0x4F, 0x42, 0x4E, 0x13, // signature 0x134E424F
            0xC3, 0xB2, 0xA1, 0x59, // seed:24 | rank:4 | four flags
            0x44, 0x33, 0x22, 0x11, // playFrames, little-endian
            0xEF, 0xBE,             // playCount, little-endian
            0x6C, 0xA9,             // checksum 0xA96C, little-endian
        };
        const Arduboy2Compat::RamEeprom &image = ArdubulletsSandbox::engine().eeprom;
        for (int i = 0; i < 16; ++i) assert(image.bytes[1008 + i] == expected[i]);
        // The checksum word is the last two bytes of the save window.
        assert(image.bytes[1022] == 0x6C && image.bytes[1023] == 0xA9);

        // A corrupted record byte must be rejected, not half-applied.
        ArdubulletsSandbox::engine().eeprom.update(1012, 0x00);
        save.onUnmount();
        delete &save;

        ArdubulletsApp &rejected = requireApp<ArdubulletsApp>();
        rejected.onMount(&canvas, &input);
        frame(rejected, canvas, 140);
        ArdubulletsSandbox::State &bad = ArdubulletsSandbox::portState();
        assert(bad.common.recordState == ArdubulletsSandbox::RECORD_INITIAL);
        assert(bad.common.isRecordDirty);
        assert(bad.common.record.gameSeed == 14348907UL); // GAME_SEED_MAX
        assert(bad.common.record.rank == 3);              // GAME_RANK_DEFAULT
        assert(bad.common.record.isBlinkLED);
        assert(!bad.common.record.isCodeManual && !bad.common.record.isDrawEdge &&
               !bad.common.record.isCleared);
        assert(bad.common.record.playFrames == 0 && bad.common.record.playCount == 0);
        // Rejection keeps the stored bytes; a conflict must not erase the save.
        assert(ArdubulletsSandbox::engine().eeprom.read(1012) == 0x00);
        assert(ArdubulletsSandbox::engine().eeprom.read(1022) == 0x6C);

        // Real write/read round trip across delete and recreate.
        bad.common.record.gameSeed = 0x00C0FFEE;
        bad.common.record.rank = 1;
        bad.common.record.isCodeManual = false;
        bad.common.record.isBlinkLED = true;
        bad.common.record.isDrawEdge = false;
        bad.common.record.isCleared = true;
        bad.common.record.playFrames = 0x0000BEE0;
        bad.common.record.playCount = 0x0007;
        bad.common.isRecordDirty = true;
        ArdubulletsSandbox::writeRecord();
        assert(ArdubulletsSandbox::engine().eeprom.bytes[1022] == 0x5A);
        assert(ArdubulletsSandbox::engine().eeprom.bytes[1023] == 0x9E);
        rejected.onUnmount();
        delete &rejected;

        ArdubulletsApp &restored = requireApp<ArdubulletsApp>();
        restored.onMount(&canvas, &input);
        frame(restored, canvas, 140);
        ArdubulletsSandbox::State &back = ArdubulletsSandbox::portState();
        assert(back.common.recordState == ArdubulletsSandbox::RECORD_STORED);
        assert(!back.common.isRecordDirty);
        assert(back.common.record.gameSeed == 0x00C0FFEE);
        assert(back.common.record.rank == 1);
        assert(!back.common.record.isCodeManual && back.common.record.isBlinkLED &&
               !back.common.record.isDrawEdge && back.common.record.isCleared);
        assert(back.common.record.playFrames == 0x0000BEE0);
        assert(back.common.record.playCount == 0x0007);
        restored.onUnmount();
        delete &restored;
    }

    printf("ardubullets runtime passed\n");
    return 0;
}
