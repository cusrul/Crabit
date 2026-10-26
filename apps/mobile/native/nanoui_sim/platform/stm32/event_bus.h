#ifndef CRABIT_NANOUI_SIM_EVENT_BUS_H
#define CRABIT_NANOUI_SIM_EVENT_BUS_H

namespace EventBus {
inline bool tryPublish(const char*, const char*, unsigned = 0) { return false; }
inline void publish(const char*, const char*) {}
inline void publish(const char*, const char*, unsigned) {}
}
extern "C" inline void BW16_Dedicated_Bridge() {}
extern "C" inline void OLED_WR_Byte(unsigned char, unsigned char) {}
extern "C" inline void USB_LOG(const char*, ...) {}

#endif
