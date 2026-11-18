// Host state-retention stress, not an AppManager or hardware heap test.
#include "no_static_engines.cpp"
#include <assert.h>
#include <stdio.h>

template<class App>
static Application *create() {
    App *app = CrabitArduboy::tryCreate<App>();
    assert(app);
    assert(CrabitArduboy::tryCreate<App>() == nullptr);
    return app;
}

int main() {
    Application *apps[] = {
        create<ArdubulletsApp>(), create<BananonsenseApp>(),
        create<ChiemagariApp>(), create<ChribocchiApp>(), create<EvasionApp>(),
        create<KnightMoveApp>(), create<ArduGoApp>(), create<MinesweeperApp>(),
        create<Game2048App>(), create<GalaxionApp>(), create<SpaceTanksApp>(),
        create<StairssweepApp>(), create<ToyokumonoApp>(), create<RyskApp>(),
        create<ReversiApp>(), create<QuartoApp>(), create<AsteroidDodgeApp>(),
        create<MemoryRhythmApp>(), create<PongApp>(), create<JumpBoyApp>(),
        create<LongCatApp>()
    };
    assert(CrabitArduboy::allocationStats().live == 21 + 13);
    const size_t retainedPeak = CrabitArduboy::allocationStats().live_bytes;
    Canvas canvas;
    InputDriver input;
    // Keep every State alive while mounting and updating other ports. Revisit
    // in reverse order without setup/reset to expose shared engine handles.
    for (Application *app : apps) {
        app->onMount(&canvas, &input);
        for (int frame = 0; frame < 20; ++frame) {
            fakeMillis += 17;
            app->onUpdate(fakeMillis);
        }
    }
    assert(CrabitArduboy::allocationStats().live_bytes == retainedPeak);
    assert(ArduGoSandbox::portState().unexpectedEepromAccess == 0);
    assert(MinesweeperSandbox::portState().unexpectedEepromAccess == 0);
    assert(GalaxionSandbox::portState().unexpectedEepromAccess == 0);
    assert(SpaceTanksSandbox::portState().unexpectedEepromAccess == 0);
    assert(RyskSandbox::portState().unexpectedEepromAccess == 0);
    assert(MemoryRhythmSandbox::portState().unexpectedEepromAccess == 0);
    assert(PongSandbox::portState().unexpectedEepromAccess == 0);
    assert(JumpBoySandbox::portState().unexpectedEepromAccess == 0);
    for (int i = 20; i >= 0; --i) {
        for (int frame = 0; frame < 20; ++frame) {
            fakeMillis += 17;
            apps[i]->onUpdate(fakeMillis);
        }
        apps[i]->onUnmount();
        delete apps[i];
    }
    assert(CrabitArduboy::allocationStats().live == 13);
    assert(CrabitArduboy::allocationStats().live_bytes == 13 * 1024);
    CrabitArduboy::releaseRetainedSaves();
    assert(CrabitArduboy::allocationStats().live == 0);
    assert(CrabitArduboy::allocationStats().live_bytes == 0);
    assert(CrabitArduboy::allocationStats().allocations ==
           CrabitArduboy::allocationStats().releases);
    printf("all21 live state retention passed: host state/save peak %lu bytes\n",
           static_cast<unsigned long>(retainedPeak));
}
