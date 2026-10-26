cmake_minimum_required(VERSION 3.22)

# ==========================================================
# 1. 基础配置与工具链
# ==========================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "MinSizeRel")
endif()

set(CMAKE_PROJECT_NAME crabit_st)

# 必须在 project() 之前引入 STM32 工具链
include("cmake/gcc-arm-none-eabi.cmake")

# 声明项目并启用 C, C++, 汇编
project(${CMAKE_PROJECT_NAME} C CXX ASM)
message("✅ Build type: " ${CMAKE_BUILD_TYPE})

# ==========================================================
# 🚨 2. 核心修复：全局强制注入“极限瘦身”指令！
# 必须放在 add_subdirectory 之前，确保 NanoUI 静态库也能吃到优化！
# ==========================================================
# C++ 极致瘦身：尺寸优化、LTO、禁用异常、禁用 RTTI、禁用线程安全静态变量
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -flto -finput-charset=UTF-8 -fexec-charset=UTF-8 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit")
# C 极致瘦身：尺寸优化、LTO
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -flto")

# 🚨 【决战修复点】链接器瘦身：加入 -Wl,-u,vTaskSwitchContext 防止 FreeRTOS 汇编函数被 LTO 误杀！
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Os -flto -Wl,--gc-sections -Wl,-u,vTaskSwitchContext")

# ==========================================================
# 3. 语言标准设置
# ==========================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# NanoUI 必须使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# ==========================================================
# 4. 创建目标与引入子模块
# ==========================================================
add_executable(${CMAKE_PROJECT_NAME})
option(CRABIT_QMI_RUN_PROBE "Awake-only QMI WoM/physical EXTI diagnostic root" OFF)
option(CRABIT_UI_ALLOC_DIAG "Diagnostic UI pool failure screen; reset required after failure" OFF)
if(CRABIT_UI_ALLOC_DIAG)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CRABIT_UI_ALLOC_DIAG=1)
endif()
if(CRABIT_QMI_RUN_PROBE)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CRABIT_QMI_RUN_PROBE=1)
endif()
option(CRABIT_COMPANION_UART_PROBE_ONLY "Diagnostic PING instead of BLE START; no BLE autostart" OFF)
if(CRABIT_COMPANION_UART_PROBE_ONLY)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CRABIT_COMPANION_UART_PROBE_ONLY=1)
endif()
option(CRABIT_COMPANION_SKIP_LEGACY_AUTOSTART "Diagnostic explicit-start only; preserve saved BLE preference" OFF)
if(CRABIT_COMPANION_SKIP_LEGACY_AUTOSTART)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CRABIT_COMPANION_SKIP_LEGACY_AUTOSTART=1)
endif()
option(CRABIT_DIAG_SKIP_ATTACH "Legacy no-op: ordinary BLE mode no longer sends ATTACH" OFF)
if(CRABIT_DIAG_SKIP_ATTACH)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CRABIT_DIAG_SKIP_ATTACH=1)
endif()
option(CRABIT_DEBUG_STAY_AWAKE "Debug only: inhibit automatic screen-off and deep sleep" OFF)
if(CRABIT_DEBUG_STAY_AWAKE)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CRABIT_DEBUG_STAY_AWAKE=1)
endif()
option(CRABIT_POWER_SCREEN_ONLY "Conservative screen-off only: keep DC1 at 3.3V and inhibit STOP2" ON)
if(CRABIT_POWER_SCREEN_ONLY)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CRABIT_POWER_SCREEN_ONLY=1)
else()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CRABIT_POWER_SCREEN_ONLY=0)
endif()

# 此时引入子目录，它们会自动继承上面的全局优化指令！
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(Core/NanoUI)

# ==========================================================
# 5. 配置源文件与链接库
# ==========================================================
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/nanoui_port.cpp  # NanoUI 硬件桥接文件
    Core/Src/companion_ble.cpp
    Core/Src/network_weather.cpp
    Core/Src/companion_trace.cpp
    Core/Src/app_bw16_flash.cpp
    Core/Src/axp2101.c
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    stm32cubemx      # 链接 CubeMX 底层库
    nanoui_core      # 链接 NanoUI 核心静态库
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # 自定义头文件目录加在这里
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # 自定义宏加在这里
)

target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # 自定义库目录加在这里
)

# ==========================================================
# 6. 现代 CMake LTO 开启方式 (可选保险)
# ==========================================================
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    message("✅ LTO/IPO 极致瘦身已确认开启！")
endif()
