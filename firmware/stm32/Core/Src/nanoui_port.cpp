#define SYS_USB_MODE 1 

#include "NanoUI/NanoUI.h"
#include "topics.h"
#include "comms_link.h"
#include "app_real/launcher.h" 
#include "app_real/app_clock.h" 
#include "sys_config.h"
#include "flash_store.h"
#include "health.h" 
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h" 
#include "semphr.h"       
#include <math.h>
#include <stdarg.h> 


#include "display.h"
#include "hw_input.h"
#include "hw_sensors.h"
#include "usb.h"
#include "i2c_bus.h"
#include "sensor_task.h"
#include "wake_log.h"
#include "uart_task.h"
#include "nanoui_port.h"
#include "board.h"
#include "global_store.h"
#include "event_bus.h"
#include "link_queue.h"


#include "app_services.h"
#include "sys_power.h"    

#include "axp2101.h"
#include "hw_ws2812.h"
#include "sensor_snapshot.h"
#include "ameba_session.h"
#include "companion_ble.h"
#include "companion_trace.h"
#include "../../../shared/companion_probe.h"
#if CRABIT_QMI_RUN_PROBE
#include "qmi_run.h"
#include "app_real/app_qmi_run_probe.h"
#endif

#include "NanoUI/core/memory_pool.h" 

extern QMI8658_Driver imu_drv;
extern Battery_Driver batt_drv;
extern PAT9125_Driver pat_drv;
extern Buzzer_Driver buzzer_drv;
extern BMP280_Driver bmp_drv;
extern AXP2101_Driver axp_drv;
extern volatile bool g_coproc_active;

std::atomic<Crabit::AmebaPhase> Crabit::ameba_phase{Crabit::AmebaPhase::Inactive};
// Called by the existing USB diagnostic dispatcher on the UI task only.
extern "C" uint16_t CrabitWakeHistory_Read(char* out, uint16_t capacity, uint32_t index) {
    return static_cast<uint16_t>(WakeHistory::format(out, capacity, index));
}



// 全局变量声明
GlobalDataStore GlobalStore = {0};
SystemConfig sys_cfg;
volatile bool g_coproc_active = false;
volatile uint32_t g_qmi_int1_cnt = 0; 
volatile uint32_t g_qmi_int2_cnt = 0; 
volatile uint32_t g_qmi_tap_cnt = 0;
volatile uint32_t g_qmi_tap_tick = 0;
volatile uint32_t g_pat_int_cnt = 0;  

// 外设与UI实例
STM32Canvas oled;
STM32Input  keys;
void STM32_ResetInputAfterWake() { keys.resetAfterWake(); }
WindowManager wm(&oled, &keys);
QMI8658_Driver imu_drv;
Battery_Driver batt_drv;
PAT9125_Driver pat_drv;
Buzzer_Driver  buzzer_drv; 
BMP280_Driver  bmp_drv;
WS2812_Driver  led_drv;
AXP2101_Driver axp_drv;


// NanoUI 内存分配适配到 FreeRTOS 堆。
class FreeRTOSMemoryAdapter : public NanoUI::MemoryAdapter {
private:
    struct Block {
        size_t size;
        Block* next;
        bool used;
    };
    // Keep UI allocations bounded so RTOS tasks and protocol queues retain headroom.
    static const size_t POOL_SIZE = 14 * 1024;
    uint8_t* pool_ptr = nullptr;
    Block* freeList = nullptr;

#if CRABIT_UI_ALLOC_DIAG
    unsigned diagnosticStage = 0;
    struct Snapshot {
        unsigned requested, used, free, largest, stackBytes, stage;
        unsigned rtosFree, rtosLargest, rtosMinimum;
    };
    Snapshot snapshot(size_t requested) const {
        Snapshot s{};
        s.requested = requested;
        s.stage = diagnosticStage;
        s.stackBytes = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
        HeapStats_t heap{};
        vPortGetHeapStats(&heap);
        s.rtosFree = heap.xAvailableHeapSpaceInBytes;
        s.rtosLargest = heap.xSizeOfLargestFreeBlockInBytes;
        s.rtosMinimum = heap.xMinimumEverFreeBytesRemaining;
        for (const Block* b = freeList; b; b = b->next) {
            if (b->used) s.used += b->size;
            else {
                s.free += b->size;
                if (b->size > s.largest) s.largest = b->size;
            }
        }
        return s;
    }
    // Diagnostic build only: stop before a failed new can dereference null.
    // Uses the existing canvas without creating Widgets or allocating memory.
    [[noreturn]] void allocationFailed(size_t requested) {
        const Snapshot s = snapshot(requested);
        oled.clear();
        char line[24];
        oled.drawText(0, 0, "UI OOM", 12);
        snprintf(line, sizeof(line), "REQ %u MAX %u", s.requested, s.largest);
        oled.drawText(0, 14, line, 8);
        const unsigned values[] = {s.free, s.rtosFree, s.rtosLargest, s.rtosMinimum};
        const char* labels[] = {"UI FREE", "RT FREE", "RT MAX", "RT MIN"};
        for (unsigned i = 0; i < 4; ++i) {
            snprintf(line, sizeof(line), "%s %u", labels[i], values[i]);
            oled.drawText(0, 24 + i * 10, line, 8);
        }
        oled.display();
        USB_LOG("[UI_OOM] stage=%u req=%u used=%u free=%u max=%u stack=%u\r\n",
            s.stage, s.requested, s.used, s.free, s.largest, s.stackBytes);
        USB_LOG("[RT_HEAP] free=%u max=%u min=%u\r\n",
            s.rtosFree, s.rtosLargest, s.rtosMinimum);
        // Retain the failure screen and shared lock; reset is required to leave this build.
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif

    public:
    FreeRTOSMemoryAdapter() {}

#if CRABIT_UI_ALLOC_DIAG
    void checkpoint(unsigned stage) {
        diagnosticStage = stage;
        const Snapshot s = snapshot(0);
        if (stage <= 2) {
            oled.clear();
            oled.drawText(0, 0, stage == 1 ? "SET ENTRY" : "SET BUILD", 12);
            char line[24];
            snprintf(line, sizeof(line), "FREE %u", s.free);
            oled.drawText(0, 18, line, 8);
            snprintf(line, sizeof(line), "MAX %u", s.largest);
            oled.drawText(0, 30, line, 8);
            snprintf(line, sizeof(line), "STACK %u", s.stackBytes);
            oled.drawText(0, 42, line, 8);
            oled.display();
        }
        USB_LOG("[UI_POOL] stage=%u used=%u free=%u max=%u stack=%u\r\n",
            stage, s.used, s.free, s.largest, s.stackBytes);
        USB_LOG("[RT_HEAP] free=%u max=%u min=%u\r\n",
            s.rtosFree, s.rtosLargest, s.rtosMinimum);
    }
    void completedFrame() {
        if (diagnosticStage == 2) checkpoint(3);
    }
#endif

    void init() {
        if (pool_ptr) return;
        pool_ptr = (uint8_t*)pvPortMalloc(POOL_SIZE);
        if (pool_ptr) {
            freeList = (Block*)pool_ptr;
            freeList->size = POOL_SIZE - sizeof(Block);
            freeList->next = nullptr;
            freeList->used = false;
            USB_LOG("✅ [MEM] UI Pool Allocated: %uB at %p\r\n", (unsigned)POOL_SIZE, pool_ptr);
        } else {
            USB_LOG("❌ [MEM] UI Pool Allocation FAILED!\r\n");
        }
    }

    void* alloc(size_t size) override {
        if (!freeList) {
#if CRABIT_UI_ALLOC_DIAG
            allocationFailed(size);
#endif
            return nullptr;
        }
        size = (size + 3) & ~3;
        Block* curr = freeList;
        while (curr) {
            if (!curr->used && curr->size >= size) {
                if (curr->size > size + sizeof(Block) + 4) {
                    Block* nextB = (Block*)((uint8_t*)curr + sizeof(Block) + size);
                    nextB->size = curr->size - size - sizeof(Block);
                    nextB->next = curr->next;
                    nextB->used = false;
                    curr->size = size;
                    curr->next = nextB;
                }
                curr->used = true;
                return (uint8_t*)curr + sizeof(Block);
            }
            curr = curr->next;
        }
#if CRABIT_UI_ALLOC_DIAG
        allocationFailed(size);
#endif
        return nullptr;
    }

    void free(void* ptr) override {
        if (!ptr || !pool_ptr || (uint8_t*)ptr < pool_ptr || (uint8_t*)ptr >= pool_ptr + POOL_SIZE) return;
        Block* b = (Block*)((uint8_t*)ptr - sizeof(Block));
        b->used = false;
        coalesce();
    }

    void coalesce() {
        Block* curr = freeList;
        while (curr && curr->next) {
            if (!curr->used && !curr->next->used) {
                curr->size += sizeof(Block) + curr->next->size;
                curr->next = curr->next->next;
                continue;
            }
            curr = curr->next;
        }
    }
};

static FreeRTOSMemoryAdapter g_rtos_allocator;
#if CRABIT_UI_ALLOC_DIAG
extern "C" void STM32_UiPoolCheckpoint(unsigned stage) {
    g_rtos_allocator.checkpoint(stage);
}
#endif

extern "C" void NanoUI_InitAllocator(void) {
    g_rtos_allocator.init();
    NanoUI::MemoryPool::instance().setAdapter(&g_rtos_allocator);
}

void NanoUI_MainLoop(void) {
    Board::init();
    Board::startTasks();

    while(1) {
        Health::beat(Health::Task::Ui);
        Health::check(HAL_GetTick());
        Usb::diagTick();
        // Feedback expiry belongs to this UI owner even while the screen is off.
        InputFeedback::tick();
        Crabit::tickFeatures(HAL_GetTick());
        // Observe only; never send a radio command or block on the USB host.
        static uint32_t trace_last = 0, trace_until = 0;
        const uint32_t trace_now = HAL_GetTick();
        if (Crabit::CompanionBle::hardwareActive()) trace_until = trace_now + 10000;
        if (int32_t(trace_until - trace_now) > 0 && uint32_t(trace_now - trace_last) >= 1000) {
            trace_last = trace_now;
            const uint16_t id = Crabit::CompanionBle::sessionId();
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::RxCounts, id,
                Uart::rxEvents(), Uart::rxErrors());
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::FrameCounts, id,
                Uart::frames(), Uart::badFrames());
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::QueueCounts, id,
                Uart::uiDrops(), LinkQueue::txDepth());
        }
        Crabit::CompanionTrace::poll();
        const uint32_t lock_started = HAL_GetTick();
        const bool i2c_locked = I2cBus::lock(100);
        WakeMetrics::maximum(WakeMetrics::ui_wait_max, HAL_GetTick() - lock_started);
        if (!i2c_locked) WakeMetrics::ui_lock_misses.fetch_add(1, std::memory_order_relaxed);
        if (i2c_locked) {
            // Legacy AMEBA_LINK did not run low-power transitions while it
            // owned the session. Preserve that policy through boot and restore.
            // First hardware candidate deliberately holds the STM32 awake during
            // communication. No STOP-mode/UART/PMU recovery while BW16 is owned.
            // BLE keeps its radio/UART alive via the STOP2 guards, but the STM32
            // display may sleep. Never touch OLED while BW16 owns the UI.
            if (!Crabit::amebaSessionActive()) SysPower::tick();
            Crabit::CompanionBle::tick();
            if (!Crabit::amebaTransitionActive()) {
                // A producer must not keep this UI/I2C iteration alive forever.
                // Outside AMEBA preserve the existing drain-to-empty behavior.
                EventBus::processPending((Crabit::amebaSessionActive() || Crabit::CompanionBle::active())
                    ? 3 : static_cast<size_t>(-1));
            }
        }
        if (SysPower::isActive()) {
            if (i2c_locked) {
                const bool frame_owned_input = AppManager::frameAppActive();
                AppManager::tick();
                // A FRAME app owns its raw input; never poll it again via WM.
                if (!frame_owned_input && !AppManager::frameAppActive()) wm.update();
                I2cBus::unlock();
            }
            if (!g_coproc_active && !AppManager::frameAppActive()) {
                // wm.markDirty(); 
                wm.drawFrame(); 
#if CRABIT_UI_ALLOC_DIAG
                g_rtos_allocator.completedFrame();
#endif
            }
            // Keep the AMEBA app's 5ms scheduling opportunity, not 16ms/32ms
            // quantization of its existing >=20ms send gate.
            vTaskDelay(pdMS_TO_TICKS(Crabit::amebaSessionActive() ? 5 : 16));
        } else {
            if (i2c_locked) I2cBus::unlock();
            vTaskDelay(pdMS_TO_TICKS(50)); 
        }
    }
}
