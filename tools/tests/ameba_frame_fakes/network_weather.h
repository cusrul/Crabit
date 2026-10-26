#pragma once
#include <stdint.h>
namespace Crabit { namespace NetworkWeather {
inline bool& activeFlag() { static bool active = true; return active; }
inline void cancel() { activeFlag() = false; }
inline void abort(const char*) { activeFlag() = false; }
inline void tick() {}
inline bool busy() { return activeFlag(); }
inline uint16_t session() { return 41; }
inline const char* status() { return busy() ? "CONNECTING" : "DONE"; }
inline const char* label() { return status(); }
} }
