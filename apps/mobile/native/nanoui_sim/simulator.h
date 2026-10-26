#ifndef CRABIT_NANOUI_SIMULATOR_H
#define CRABIT_NANOUI_SIMULATOR_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(CRABIT_NANOUI_SIM_BUILD)
#    define CRABIT_NANOUI_SIM_API __declspec(dllexport)
#  else
#    define CRABIT_NANOUI_SIM_API __declspec(dllimport)
#  endif
#else
#  define CRABIT_NANOUI_SIM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

CRABIT_NANOUI_SIM_API int32_t crabit_sim_start(void);
CRABIT_NANOUI_SIM_API void crabit_sim_stop(void);
CRABIT_NANOUI_SIM_API void crabit_sim_step(uint32_t now_ms);
/* key: 0=up, 1=down, 2=OK; pressed: 1=down, 0=short release, -1=cancel. */
CRABIT_NANOUI_SIM_API void crabit_sim_button(int32_t key, int32_t pressed, uint32_t now_ms);
/* Borrowed 1024-byte row-major 128x64 packed-MSB frame; valid until stop. */
CRABIT_NANOUI_SIM_API const uint8_t* crabit_sim_frame(void);
CRABIT_NANOUI_SIM_API uint32_t crabit_sim_generation(void);
CRABIT_NANOUI_SIM_API int32_t crabit_sim_set_clock(uint32_t local_epoch_seconds);
CRABIT_NANOUI_SIM_API int32_t crabit_sim_set_weather(uint32_t date_yyyymmdd, int32_t wmo_code,
                                                      int32_t low_tenths, int32_t high_tenths,
                                                      int32_t rain_percent, uint32_t fetched_epoch_seconds);
CRABIT_NANOUI_SIM_API int32_t crabit_sim_show_clock(void);

#ifdef __cplusplus
}
#endif

#endif
