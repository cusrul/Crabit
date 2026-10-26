#ifndef CRABIT_COMPANION_PROBE_H
#define CRABIT_COMPANION_PROBE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Explicit diagnostic build only; ordinary builds retain the product route.
#ifndef CRABIT_COMPANION_UART_PROBE_ONLY
#define CRABIT_COMPANION_UART_PROBE_ONLY 0
#endif
#ifndef CRABIT_COMPANION_SKIP_LEGACY_AUTOSTART
#define CRABIT_COMPANION_SKIP_LEGACY_AUTOSTART 0
#endif
namespace Crabit { namespace CompanionProbe {
constexpr bool Enabled = CRABIT_COMPANION_UART_PROBE_ONLY != 0;
constexpr bool SkipLegacyAutostart = Enabled || CRABIT_COMPANION_SKIP_LEGACY_AUTOSTART != 0;
inline const char* topic() { return Enabled ? "sys/bw/ping" : "sys/comm"; }
inline const char* value() { return Enabled ? "PING" : "START"; }
inline bool isPong(const char* value) { return value && strcmp(value, "BW16 PONG OK!") == 0; }
struct ByteSummary { uint16_t size, zero, ff, printable; };
inline ByteSummary summarize(const uint8_t* bytes, size_t size) {
    ByteSummary result{};
    if (!bytes) return result;
    if (size > 511) size = 511;
    result.size = uint16_t(size);
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] == 0) ++result.zero;
        else if (bytes[i] == 255) ++result.ff;
        else if (bytes[i] >= 32 && bytes[i] <= 126) ++result.printable;
    }
    return result;
}
} }
#endif
