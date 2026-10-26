#include "qmi_wake_trace.h"
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
    using namespace QmiWakeTrace;
    last = {};
    Bus b;
    captureConfig(b, 0);
    assert(b.calls == 0);
    captureConfig(b, 0xD4);
    assert(last.valid == 63 && b.calls == 6 && b.statusReads == 0);
    assert(last.regs[0] == 2 && last.regs[5] == 12);
    captureStatus(b, 0xD4);
    assert(b.statusReads == 0);
    last.prepared = true;
    captureStatus(b, 0xD4);
    assert(b.statusReads == 0);
    last.motion = true;
    captureStatus(b, 0xD4);
    assert(b.statusReads == 0);  // Entry aborted: no clear.
    last.returned = true;
    captureStatus(b, 0);
    assert(b.statusReads == 0);
    captureStatus(b, 0xD4);
    assert(b.statusReads == 1 && last.statusOk && last.status == 4);
    for (unsigned i = 1; i <= 6; ++i) {
        last = {};
        Bus failed;
        failed.failAt = i;
        captureConfig(failed, 0xD4);
        assert(last.valid == (63 ^ (1u << (i - 1))) && failed.statusReads == 0);
    }
    last = {};
    last.prepared = last.motion = last.returned = true;
    Bus failed;
    failed.failAt = 1;
    captureStatus(failed, 0xD4);
    assert(!last.statusOk);
    puts(
        "PASS: register snapshot, no premature read-to-clear, absent sensor, partial read "
        "failures");
}
