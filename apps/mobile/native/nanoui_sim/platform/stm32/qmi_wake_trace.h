#ifndef CRABIT_NANOUI_SIM_QMI_WAKE_TRACE_H
#define CRABIT_NANOUI_SIM_QMI_WAKE_TRACE_H
#include <stdint.h>
namespace QmiWakeTrace {
struct Trace {
    uint8_t beforeLevels = 0, beforePending = 0, mask = 0, map = 0;
    uint8_t valid = 0, prepared = 0, regs[6] = {}, afterLevels = 0, afterPending = 0;
    uint8_t returned = 0, armed = 0, status = 0;
    bool statusOk = true;
};
inline Trace last{};
}
#endif
