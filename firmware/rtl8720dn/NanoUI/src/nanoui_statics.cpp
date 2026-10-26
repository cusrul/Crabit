#include "NanoUI.h"
#include "core/data_hub.h"
#include "CrabitBuildProfile.h"
#include <vector>
#include <string>

// App 层全局状态定义。
#if CRABIT_BW16_ENABLE_WIFI_LAB
#include "../app/wifi_hack_core.h"
#endif

using namespace NanoUI;

// Physical Storage for NanoUI Static Members
// NanoUI static storage is owned by the shared library runtime.

SystemData DataHub::shared = {0, 0, 0, {100, 4.2f, true}};

// App 全局变量的存储定义。
#if CRABIT_BW16_ENABLE_WIFI_LAB
WifiHackContext* g_hackCtx = nullptr;
#endif
