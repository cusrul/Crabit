#include "CrabitBuildProfile.h"

#if CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_WIFI_TOOLS_LAB || CRABIT_BW16_ENABLE_NETWORK_SYNC
#include <stdlib.h>

// GCC libstdc++'s default fatal handler also links the C++ type-name demangler.
// This profile keeps new/delete, exceptions and the fatal abort behavior;
// only the verbose, uncaught-exception type-name report is omitted.
// Toolchain-specific: validate the linked symbols when upgrading Realtek GCC.
namespace __gnu_cxx {
void __verbose_terminate_handler() {
    abort();
}
}  // namespace __gnu_cxx
#endif
