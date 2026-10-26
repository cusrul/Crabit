#include "companion_session.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

using Crabit::CompanionSession;
using Crabit::CompanionShortcut;

static void bootToStarting(CompanionSession& s, uint16_t id = 41,
                           uint32_t startAt = 100) {
    assert(s.start(id));
    assert(s.tick(startAt) == CompanionSession::Action::Boot);
    s.booted(startAt + 50);
    assert(s.tick(startAt + 50 + 1199) == CompanionSession::Action::None);
    assert(s.tick(startAt + 50 + 1200) == CompanionSession::Action::PrepareAndStart);
    s.started(true, startAt + 1300);
}

static void reachConnected(CompanionSession& s, uint16_t id = 41) {
    bootToStarting(s, id);
    s.receive(id, "READY");
    assert(s.phase == CompanionSession::Phase::Advertising);
    s.receive(id, "CONNECTED");
    assert(s.phase == CompanionSession::Phase::Connected);
}

int main() {
    // Exact wrapper action sequence: boot, 1200 ms wait, prepare/start, READY.
    CompanionSession s;
    assert(s.start(41));
    assert(s.tick(100) == CompanionSession::Action::Boot);
    assert(s.phase == CompanionSession::Phase::Booting);
    s.booted(150);
    assert(s.tick(1349) == CompanionSession::Action::None);
    assert(s.tick(1350) == CompanionSession::Action::PrepareAndStart);
    s.started(true, 1400);
    s.receive(40, "READY");
    assert(s.phase == CompanionSession::Phase::Starting); // Wrong-session READY ignored.
    // A matched owner CONNECTED is stronger evidence than READY. Accept it if
    // the earlier UART status was lost; never infer connection from QUEUED.
    s.receive(41, "CONNECTED");
    assert(s.phase == CompanionSession::Phase::Connected);
    s.receive(41, "READY");
    assert(s.phase == CompanionSession::Phase::Connected);
    s.receive(41, "READY"); // READY is ignored after connection.
    assert(s.phase == CompanionSession::Phase::Connected);
    s.receive(41, "CONNECTED");
    assert(s.phase == CompanionSession::Phase::Connected);

    // Startup timeout preserves failure through the required power-off action.
    CompanionSession startupTimeout;
    bootToStarting(startupTimeout, 51, 200);
    startupTimeout.receive(50, "FAILED");
    const uint32_t startSince = 1500;
    assert(startupTimeout.tick(startSince + 6999) == CompanionSession::Action::None);
    assert(startupTimeout.tick(startSince + 7000) == CompanionSession::Action::PowerOff);
    assert(startupTimeout.phase == CompanionSession::Phase::PoweringOff);
    startupTimeout.poweredOff();
    assert(startupTimeout.phase == CompanionSession::Phase::Failed);
    assert(startupTimeout.status && std::strcmp(startupTimeout.status, "START TIMEOUT") == 0);
    assert(startupTimeout.stop() == false);

    // Stop before first boot tick must not invoke Boot or any other action.
    CompanionSession earlyStop;
    assert(earlyStop.start(61));
    assert(earlyStop.stop());
    assert(earlyStop.phase == CompanionSession::Phase::Off);
    assert(earlyStop.tick(0) == CompanionSession::Action::None);

    // Normal STOPPED acknowledgment requests exactly one power-off action.
    CompanionSession normalStop;
    reachConnected(normalStop, 71);
    assert(normalStop.stop());
    assert(normalStop.tick(5000) == CompanionSession::Action::Stop);
    normalStop.receive(70, "STOPPED");
    normalStop.receive(71, "READY");
    normalStop.receive(71, "CONNECTED");
    assert(normalStop.phase == CompanionSession::Phase::Stopping);
    normalStop.receive(71, "STOPPED");
    assert(normalStop.tick(5001) == CompanionSession::Action::PowerOff);
    normalStop.poweredOff();
    assert(normalStop.phase == CompanionSession::Phase::Off);

    // Stop timeout also powers off, but retains the timeout failure.
    CompanionSession stopTimeout;
    reachConnected(stopTimeout, 81);
    assert(stopTimeout.stop());
    assert(stopTimeout.tick(6000) == CompanionSession::Action::Stop);
    assert(stopTimeout.tick(7999) == CompanionSession::Action::None);
    assert(stopTimeout.tick(8000) == CompanionSession::Action::PowerOff);
    assert(stopTimeout.status && stopTimeout.status[0] == 'S');
    stopTimeout.poweredOff();
    assert(stopTimeout.phase == CompanionSession::Phase::Failed);
    assert(std::strcmp(stopTimeout.status, "STOP TIMEOUT") == 0);

    // UART/queue start failure likewise powers off and preserves its error.
    CompanionSession startFailure;
    assert(startFailure.start(91));
    assert(startFailure.tick(10) == CompanionSession::Action::Boot);
    startFailure.booted(10);
    assert(startFailure.tick(1210) == CompanionSession::Action::PrepareAndStart);
    startFailure.started(false, 1210);
    assert(startFailure.tick(1210) == CompanionSession::Action::PowerOff);
    startFailure.poweredOff();
    assert(startFailure.phase == CompanionSession::Phase::Failed);
    assert(startFailure.status && std::strcmp(startFailure.status, "UART/QUEUE FAIL") == 0);

    // Pair sequence parsing: positive uint32 values, overflow and malformed rejected.
    CompanionSession pair;
    reachConnected(pair, 101);
    pair.receive(101, "PAIR_WAIT:1");
    assert(pair.pair == 1 && pair.canConfirm(101, 1));
    assert(!pair.canConfirm(100, 1) && !pair.canConfirm(101, 2));
    pair.receive(101, "PAIR_WAIT:4294967295");
    assert(pair.pair == UINT32_MAX && pair.canConfirm(101, UINT32_MAX));
    const uint32_t maxSequence = pair.pair;
    pair.receive(101, "PAIR_WAIT:4294967296");
    assert(pair.pair == maxSequence);
    pair.receive(101, "PAIR_WAIT:12x");
    assert(pair.pair == maxSequence);
    pair.receive(101, "PAIR_WAIT:0");
    assert(pair.pair == maxSequence);
    pair.receive(101, "PAIR_WAIT:");
    assert(pair.pair == maxSequence);
    pair.receive(100, "PAIR_WAIT:2");
    assert(pair.pair == maxSequence);
    pair.receive(101, "PAIR_WAIT:24");
    assert(pair.pair == 24 && !pair.canConfirm(101, maxSequence));
    pair.receive(101, "PAIR_WAIT:25");
    assert(pair.pair == 25 && !pair.canConfirm(101, 24));

    // Every policy edge invalidates old confirmation sequence.
    pair.receive(101, "DISCONNECTED");
    assert(pair.pair == 0 && !pair.canConfirm(101, 25));
    pair.receive(101, "CONNECTED");
    pair.receive(101, "PAIR_WAIT:20");
    pair.receive(101, "PAIR_TIMEOUT");
    assert(pair.pair == 0 && !pair.canConfirm(101, 20));
    pair.receive(101, "PAIR_WAIT:21");
    pair.receive(101, "PAIR_CANCELLED");
    assert(pair.pair == 0 && !pair.canConfirm(101, 21));
    pair.receive(101, "PAIR_WAIT:22");
    pair.receive(101, "AUTHENTICATED");
    assert(pair.pair == 0 && !pair.canConfirm(101, 22));

    CompanionSession pairStop;
    reachConnected(pairStop, 111);
    pairStop.receive(111, "PAIR_WAIT:23");
    assert(pairStop.stop());
    assert(pairStop.pair == 0 && !pairStop.canConfirm(111, 23));

    // Unsigned elapsed-time subtraction is wrap-safe for session and shortcut.
    CompanionSession wrap;
    assert(wrap.start(121));
    assert(wrap.tick(UINT32_MAX - 500) == CompanionSession::Action::Boot);
    wrap.booted(UINT32_MAX - 400);
    assert(wrap.tick(798) == CompanionSession::Action::None);
    assert(wrap.tick(799) == CompanionSession::Action::PrepareAndStart);

    // Chord needs 1000 ms; single-key bounce resets debounce; latch until both up.
    CompanionShortcut shortcut;
    assert(!shortcut.update(true, true, 100));
    assert(!shortcut.update(true, true, 1099));
    assert(shortcut.update(true, true, 1100));
    assert(!shortcut.update(true, true, 1101));
    assert(!shortcut.update(false, false, 1200));
    assert(!shortcut.update(true, false, 1201));
    assert(!shortcut.update(true, true, 1202));
    assert(!shortcut.update(true, true, 2201));
    assert(shortcut.update(true, true, 2202));
    assert(!shortcut.update(false, true, 2203));
    assert(!shortcut.update(true, false, 2204));
    assert(!shortcut.update(false, false, 2205));
    assert(!shortcut.update(true, true, 2206));
    assert(!shortcut.update(true, true, 3205));
    assert(shortcut.update(true, true, 3206));
    assert(!shortcut.update(false, false, 3207));
    assert(!shortcut.update(true, true, 4000));
    assert(shortcut.update(true, true, 5000));

    CompanionShortcut shortcutWrap;
    assert(!shortcutWrap.update(true, true, UINT32_MAX - 500));
    assert(!shortcutWrap.update(true, true, 498));
    assert(shortcutWrap.update(true, true, 499));

    CompanionSession lostPairReply;
    reachConnected(lostPairReply);
    lostPairReply.receive(41, "PAIR_WAIT:45", 1000);
    assert(lostPairReply.canConfirm(41, 45));
    lostPairReply.tick(32000);
    assert(!lostPairReply.canConfirm(41, 45));
    assert(lostPairReply.phase == CompanionSession::Phase::Connected);

    lostPairReply.receive(41, "AUTHENTICATED", 33000);
    lostPairReply.receive(41, "PAIR_WAIT:46", 33100);
    assert(lostPairReply.phase == CompanionSession::Phase::Connected);
    assert(lostPairReply.canConfirm(41, 46));
    assert(!lostPairReply.canConfirm(41, 45));

    std::cout << "companion session policy assertions passed\n";
}
