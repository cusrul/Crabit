#ifndef CRABIT_NANOUI_SIM_WAKE_METRICS_H
#define CRABIT_NANOUI_SIM_WAKE_METRICS_H
#include <atomic>
namespace WakeMetrics { inline std::atomic<uint32_t> stop_hclk{80000000u}; }
extern uint32_t SystemCoreClock;
#endif
