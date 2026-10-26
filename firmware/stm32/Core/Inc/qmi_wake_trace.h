#pragma once
#include <stdint.h>

// Written only by the paused STOP2 owner; read by the UI after it resumes.
// STATUS1 is intentionally read only AFTER wake, before WoM is dismantled.
namespace QmiWakeTrace {
struct Snapshot {
    uint32_t attempts = 0;
    bool motion = false, prepared = false, statusOk = false, armed = false, returned = false;
    uint8_t regs[6]{};  // CTRL1, CTRL2, CTRL7, CTRL8, CAL1_L, CAL1_H
    uint8_t valid = 0, beforeLevels = 0, afterLevels = 0;
    uint8_t beforePending = 0, afterPending = 0, mask = 0, map = 0, status = 0;
};
inline Snapshot last{};
template <class Bus>
void captureConfig(Bus& bus, uint16_t address) {
    constexpr uint8_t registers[] = {0x02, 0x03, 0x08, 0x09, 0x0B, 0x0C};
    if (!address)
        return;
    for (unsigned i = 0; i < 6; ++i)
        if (bus.read(address, registers[i], last.regs[i]))
            last.valid |= uint8_t(1u << i);
}
template <class Bus>
void captureStatus(Bus& bus, uint16_t address) {
    if (address && last.prepared && last.motion && last.returned)
        last.statusOk = bus.read(address, 0x2F, last.status);
}
}  // namespace QmiWakeTrace
