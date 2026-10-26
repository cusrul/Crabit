#include <assert.h>
#include <initializer_list>
#include <stdint.h>
#include <string.h>

#include "../../firmware/rtl8720dn/NanoUI/RadioSvc.h"
#include "../../firmware/shared/companion_session.h"

static void parses_commands_strictly() {
    char action[24]{}; uint32_t sequence = 99;
    assert(RadioSvc::parseCompanionValue("START", action, sizeof(action), &sequence));
    assert(strcmp(action, "START") == 0 && sequence == 0);
    assert(!RadioSvc::parseCompanionValue("START:0", action, sizeof(action), &sequence));
    assert(!RadioSvc::parseCompanionValue("STOP:1", action, sizeof(action), &sequence));
    assert(RadioSvc::parseCompanionValue("CONFIRM:42", action, sizeof(action), &sequence));
    assert(strcmp(action, "CONFIRM") == 0 && sequence == 42);
    assert(RadioSvc::parseCompanionValue("CANCEL:4294967295", action, sizeof(action), &sequence));
    assert(sequence == UINT32_MAX);
    for (const char* bad : {"CONFIRM:-1", "CONFIRM:+1", "CONFIRM: 1", "CONFIRM:4294967296"}) {
        assert(RadioSvc::parseCompanionValue(bad, action, sizeof(action), &sequence));
        assert(sequence == 0);
    }
    assert(RadioSvc::parseCompanionValue("CONFIRM:", action, sizeof(action), &sequence));
    assert(sequence == 0);
}

static void pair_deadline_is_wrap_safe() {
    assert(!RadioSvc::pairDeadlineExpired(99, 100));
    assert(RadioSvc::pairDeadlineExpired(100, 100));
    assert(RadioSvc::pairDeadlineExpired(101, 100));
    assert(!RadioSvc::pairDeadlineExpired(UINT32_MAX - 4, 3));
    assert(!RadioSvc::pairDeadlineExpired(2, 3));
    assert(RadioSvc::pairDeadlineExpired(3, 3));
}

static void session_guards_pair_confirmation_and_stop() {
    Crabit::CompanionSession session;
    assert(!session.start(0));
    assert(session.start(77));
    assert(session.tick(10) == Crabit::CompanionSession::Action::Boot);
    session.booted(20);
    assert(session.tick(1219) == Crabit::CompanionSession::Action::None);
    assert(session.tick(1220) == Crabit::CompanionSession::Action::PrepareAndStart);
    session.started(true, 1221);
    session.receive(76, "READY");
    assert(session.phase == Crabit::CompanionSession::Phase::Starting);
    session.receive(77, "READY");
    session.receive(77, "CONNECTED");
    session.receive(77, "PAIR_WAIT:123");
    assert(session.canConfirm(77, 123));
    assert(!session.canConfirm(76, 123));
    assert(!session.canConfirm(77, 122));
    session.receive(77, "PAIR_REJECTED");
    assert(session.phase == Crabit::CompanionSession::Phase::Connected);
    assert(session.stop());
    assert(session.tick(1300) == Crabit::CompanionSession::Action::Stop);
    session.receive(77, "STOPPED");
    assert(session.tick(1301) == Crabit::CompanionSession::Action::PowerOff);
    session.poweredOff();
    assert(session.phase == Crabit::CompanionSession::Phase::Off);
}

int main() {
    parses_commands_strictly();
    pair_deadline_is_wrap_safe();
    session_guards_pair_confirmation_and_stop();
}
