#include "wake_log.h"
#include <cassert>
#include <cstdio>
struct Bus {
    unsigned calls = 0, statusReads = 0, failAt = 0;
    bool read(uint16_t addr, uint8_t reg, uint8_t& value) {
        assert(addr == 0xD4);
        ++calls;
        if (reg == 0x2F)
            ++statusReads;
        if (calls == failAt)
            return false;
        value = reg == 0x2F ? 4 : reg;
        return true;
    }
};
int main() {
    using QT = QmiWakeTrace;
    QT::last = {};
    Bus b;
    QT::captureConfig(b, 0);
    assert(b.calls == 0);
    QT::captureConfig(b, 0xD4);
    assert(QT::last.valid == 63 && b.calls == 6 && b.statusReads == 0);
    assert(QT::last.regs[0] == 2 && QT::last.regs[5] == 12);
    QT::captureStatus(b, 0xD4);
    assert(b.statusReads == 0);
    QT::last.prepared = true;
    QT::captureStatus(b, 0xD4);
    assert(b.statusReads == 0);
    QT::last.motion = true;
    QT::captureStatus(b, 0xD4);
    assert(b.statusReads == 0);  // Entry aborted: no clear.
    QT::last.returned = true;
    QT::captureStatus(b, 0);
    assert(b.statusReads == 0);
    QT::captureStatus(b, 0xD4);
    assert(b.statusReads == 1 && QT::last.statusOk && QT::last.status == 4);
    for (unsigned i = 1; i <= 6; ++i) {
        QT::last = {};
        Bus failed;
        failed.failAt = i;
        QT::captureConfig(failed, 0xD4);
        assert(QT::last.valid == (63 ^ (1u << (i - 1))) && failed.statusReads == 0);
    }
    QT::last = {};
    QT::last.prepared = QT::last.motion = QT::last.returned = true;
    Bus failed;
    failed.failAt = 1;
    QT::captureStatus(failed, 0xD4);
    assert(!QT::last.statusOk);
    puts(
        "PASS: register snapshot, no premature read-to-clear, absent sensor, partial read "
        "failures");
}
