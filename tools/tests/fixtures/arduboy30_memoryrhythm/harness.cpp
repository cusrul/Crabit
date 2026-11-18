// Host harness for the actual tuxinator2009 Memory Rhythm source. Only Canvas,
// InputDriver, AmebaStore telemetry and Arduino timing/random are fake.
#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))

#define STATE_PLAY 0x1111
#define STATE_LEARN 0x2222
#define STATE_DONE 0x3333
#define ACTION_JUMP 0x4444
#define ACTION_DONE 0x5555
#define TONE_HIGH_VOLUME 0x6666
#define CLEAR_BUFFER 0x7777
#define BITMAP 0x8888

#include "app/apps/games/memoryrhythm/app.h"
#if STATE_PLAY != 0x1111 || STATE_LEARN != 0x2222 || STATE_DONE != 0x3333 || \
    ACTION_JUMP != 0x4444 || ACTION_DONE != 0x5555 || \
    TONE_HIGH_VOLUME != 0x6666 || CLEAR_BUFFER != 0x7777 || BITMAP != 0x8888
#error "Memory Rhythm did not restore preceding macros"
#endif
#undef STATE_PLAY
#undef STATE_LEARN
#undef STATE_DONE
#undef ACTION_JUMP
#undef ACTION_DONE
#undef TONE_HIGH_VOLUME
#undef CLEAR_BUFFER
#undef BITMAP

#include <assert.h>
#include <stdio.h>
#include <string.h>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
}

namespace MR = MemoryRhythmSandbox;

static float g_ax = 0, g_ay = 0;
static void tilt(float ax, float ay) { g_ax = ax; g_ay = ay; }
static void level() { g_ax = g_ay = 0; }

static void frames(Application &app, unsigned count, uint8_t raw = 0) {
    AmebaStore::raw_btn_mask = raw;
    AmebaStore::imu_ax = g_ax;
    AmebaStore::imu_ay = g_ay;
    AmebaStore::imu_az = 1.0f;
    for (unsigned i = 0; i < count; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
    }
}
static void tap(Application &app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 4, 0);
}
static void move(Application &app, float ax, float ay) {
    tilt(ax, ay);
    frames(app, 1);
    level();
    frames(app, 1);
}
static void pressAction(Application &app, uint8_t action) {
    switch (action) {
    case 1: move(app, 0.0f, -0.5f); break; // UP
    case 2: move(app, 0.0f, 0.5f); break;  // DOWN
    case 3: move(app, -0.5f, 0.0f); break; // LEFT
    case 4: move(app, 0.5f, 0.0f); break;  // RIGHT
    case 5: tap(app, 1U << 4); break;      // A
    case 6: tap(app, 1U << 5); break;      // B
    default: assert(false && "invalid action");
    }
}
static bool nonEmpty(const Canvas &canvas) {
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x)
            if (canvas.pixels[y][x]) return true;
    return false;
}
static uint64_t hashCanvas(const Canvas &canvas) {
    uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x) {
            h ^= canvas.pixels[y][x] ? 1ULL : 0ULL;
            h *= 1099511628211ULL;
        }
    return h;
}
static void savePbm(const Canvas &canvas, const char *dir, const char *name) {
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pbm", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P1\n128 64\n");
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; ++x) fputc(canvas.pixels[y][x] ? '0' : '1', f);
        fputc('\n', f);
    }
    fclose(f);
}

static unsigned collectLearn(Application &app, Canvas &canvas, const char *dir,
                             const char *prefix, uint8_t *actions, unsigned limit) {
    unsigned n = 0;
    unsigned guard = 0;
    uint8_t previousDuration = MR::portState().game.actionDuration;
    while (MR::portState().game.state == MR::STATE_LEARN && n < limit && guard++ < 4000) {
        frames(app, 1);
        if (MR::portState().game.state == MR::STATE_LEARN && MR::portState().game.actionDuration == 16 &&
            previousDuration != 16 && MR::portState().game.action != 0) {
            actions[n++] = MR::portState().game.action;
            char name[64];
            snprintf(name, sizeof(name), "%s-%u", prefix, n);
            savePbm(canvas, dir, name);
        }
        previousDuration = MR::portState().game.actionDuration;
    }
    return n;
}
static void waitForIdle(Application &app, unsigned guardLimit = 4000) {
    unsigned guard = 0;
    while (MR::portState().game.actionDuration != 0 && guard++ < guardLimit) frames(app, 1);
    assert(guard < guardLimit);
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
    const char *dir = argc > 1 ? argv[1] : nullptr;
    Point nanoPoint{2, 3};
    Rect nanoRect{0, 0, 4, 4};
    assert(nanoPoint.x == 2 && nanoRect.width() == 4);

    Canvas canvas;
    InputDriver input;
    MemoryRhythmApp &app = requireApp<MemoryRhythmApp>();
    app.onMount(&canvas, &input);
    frames(app, 3);
    assert(MR::portState().game.state == MR::STATE_DONE);
    assert(MR::portState().game.score == 0 && MR::portState().game.numMoves == 3);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "memoryrhythm-start");

    // Verify the six silent action poses are visually distinct using the real
    // icons/sprite assets and the same draw calls as loop(). This is what a
    // player must read during the silent LEARN sequence.
    uint64_t pose[7] = {0};
    for (uint8_t a = 1; a <= 6; ++a) {
        MR::portState().game.arduboy.clear();
        MR::portState().game.sprites.drawSelfMasked(112, 0, MR::icons, a);
        MR::portState().game.sprites.drawSelfMasked(52, 24, MR::sprite, a);
        MR::portState().game.arduboy.display();
        pose[a] = hashCanvas(canvas);
    }
    for (uint8_t a = 1; a <= 6; ++a)
        for (uint8_t b = a + 1; b <= 6; ++b)
            assert(pose[a] != pose[b]);
    frames(app, 1); // restore the game-owned frame

    // Start the original DONE -> LEARN sequence. Only one frame is advanced so
    // collectLearn() observes the first generated move instead of missing it.
    frames(app, 1, 1U << 4);
    assert(MR::portState().game.state == MR::STATE_LEARN);
    assert(MR::portState().game.actionDuration == 0);

    uint8_t first[8] = {0};
    unsigned learned = collectLearn(app, canvas, dir, "memoryrhythm-learn", first, 8);
    assert(learned == 3);
    assert(MR::portState().game.state == MR::STATE_PLAY);
    assert(MR::portState().game.currentMove == 0);
    for (unsigned i = 0; i < learned; ++i)
        assert(first[i] >= 1 && first[i] <= 6);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "memoryrhythm-play");

    // Replay the displayed sequence with the original directions/A/B actions.
    for (unsigned i = 0; i < learned; ++i) {
        waitForIdle(app);
        assert(MR::portState().game.state == MR::STATE_PLAY);
        pressAction(app, first[i]);
        assert(MR::portState().game.state == MR::STATE_PLAY);
        assert(MR::portState().game.score == i + 1);
    }
    waitForIdle(app);
    assert(MR::portState().game.state == MR::STATE_LEARN);
    assert(MR::portState().game.score == 6 && MR::portState().game.numMoves == 4 && MR::portState().game.octave == 0);
    savePbm(canvas, dir, "memoryrhythm-advance");

    // Correct progress produces a longer four-move sequence.
    uint8_t second[8] = {0};
    unsigned learned2 = collectLearn(app, canvas, dir, "memoryrhythm-learn2", second, 8);
    assert(learned2 == 4);
    assert(MR::portState().game.state == MR::STATE_PLAY);
    savePbm(canvas, dir, "memoryrhythm-learn4");

    // A wrong action uses the original error path and stops the round.
    waitForIdle(app);
    assert(MR::portState().game.state == MR::STATE_PLAY);
    uint8_t wrong = uint8_t(second[0] % 6 + 1);
    assert(wrong != second[0]);
    pressAction(app, wrong);
    assert(MR::portState().game.state == MR::STATE_DONE);
    assert(MR::portState().game.action == MR::ACTION_DONE);
    savePbm(canvas, dir, "memoryrhythm-done");

    // A restarts with score 0, speed 1 and a three-move sequence.
    tap(app, 1U << 4);
    assert(MR::portState().game.state == MR::STATE_LEARN);
    assert(MR::portState().game.score == 0 && MR::portState().game.octave == 0 && MR::portState().game.numMoves == 3 &&
           MR::portState().game.actionRate == 5 && MR::portState().game.currentMove == 0);
    savePbm(canvas, dir, "memoryrhythm-restart");

    // Re-entry returns to the original DONE/start screen.
    app.onUnmount();
    app.onMount(&canvas, &input);
    frames(app, 2);
    assert(MR::portState().game.state == MR::STATE_DONE);
    assert(MR::portState().game.score == 0 && MR::portState().game.numMoves == 3);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "memoryrhythm-reentry");

    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 17);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 0);
    puts("memoryrhythm actual game runtime passed");
    delete &app;
}
