// Sensor sampling service: snapshot publication, polling and EXTI counters.
#include "sensor_task.h"

#include "ameba_session.h"
#include "companion_ble.h"
#include "health.h"
#include "hw_sensors.h"
#include "i2c_bus.h"
#include "main.h"
#include "sensor_snapshot.h"
#include "sys_power.h"
#include "usb.h"
#include "wake_log.h"

#include "FreeRTOS.h"
#include "task.h"

#if CRABIT_QMI_RUN_PROBE
#include "qmi_run.h"
#endif

extern QMI8658_Driver imu_drv;

Crabit::SnapshotStore<StmSensorSnapshot> g_stm_sensor_snapshot;

bool STM32_ReadSensorSnapshot(StmSensorSnapshot* out) {
    return g_stm_sensor_snapshot.read(out);
}

// Trial-only USB trace. Read under the sensor bus lock, stop on first error,
// and never print while holding the lock. 0x100 is outside the byte range.
struct BatteryProbe {
    static constexpr uint8_t regs[] = {
        AXP2101_BAT_PERCENT_DATA, AXP2101_FUEL_GAUGE_CTRL,
        AXP2101_CHARGE_GAUGE_WDT_CTRL, AXP2101_STATUS1, AXP2101_STATUS2,
        AXP2101_BAT_DET_CTRL, AXP2101_ADC_DATA_RELUST0, AXP2101_ADC_DATA_RELUST1,
    };
    uint8_t raw[sizeof(regs)]{};
    unsigned count = 0;

    void read() {
        while (count < sizeof(regs) && I2cBus::read(AXP2101_ADDR, regs[count], &raw[count], 1, 5))
            ++count;
    }

    unsigned reg(unsigned i) const { return i < count ? raw[i] : 0x100u; }

    void log(uint32_t tick) const {
        const int mv = count == sizeof(regs) ? ((raw[6] & 0x1f) << 8) | raw[7] : -1;
        const int charging = count >= 5 ? ((raw[4] & 0x60) == 0x20 ? 1 : 0) : -1;
        USB_LOG("[BAT] at=%lu A4=%02X A2=%02X 18=%02X 00=%02X 01=%02X 68=%02X mv=%d charging=%d\r\n",
                (unsigned long)tick, reg(0), reg(1), reg(2), reg(3), reg(4), reg(5), mv, charging);
    }
};

static void PublishSensorSnapshot(uint32_t now_ms) {
    StmSensorSnapshot snapshot{};
    snapshot.published_at = now_ms;
    auto& sensors = NanoUI::SensorManager::getInstance();
    if (auto* sensor = sensors.getSensor("QMI8658")) {
        snapshot.imu = *static_cast<const NanoUI::SensorDataIMU*>(sensor->getData());
    }
    if (auto* sensor = sensors.getSensor("PAT9125")) {
        snapshot.optical = *static_cast<const NanoUI::SensorDataOptical*>(sensor->getData());
    }
    if (auto* sensor = sensors.getSensor("BMP280")) {
        snapshot.pressure = *static_cast<const NanoUI::SensorDataPress*>(sensor->getData());
    }
    if (auto* sensor = sensors.getSensor("BATTERY")) {
        snapshot.battery = *static_cast<const NanoUI::SensorDataBattery*>(sensor->getData());
    }
    if (auto* sensor = sensors.getSensor("AXP2101")) {
        // This copies the existing AXP-owned result; it does not call AXP.
        snapshot.power = *static_cast<const NanoUI::SensorDataAXP2101*>(sensor->getData());
    }
    g_stm_sensor_snapshot.publish(snapshot);
}

void Task_SensorHub(void *pvParameters) {
    (void)pvParameters;
    uint32_t last_log_tick = 0;
    uint32_t last_battery_probe = 0;
    
    while (1) {
        Health::beat(Health::Task::Sensor);
        if (SysPower::isActive()) { 
            if (I2cBus::recoverySyncWake(WakeMetrics::wakes.load(std::memory_order_relaxed))) {
                I2cFault::paused.store(false, std::memory_order_relaxed);
                I2cFault::latched.store(false, std::memory_order_relaxed);
                I2cFault::attempts.store(0, std::memory_order_relaxed);
            }
            if ((!I2cFault::paused.load(std::memory_order_relaxed) ||
                 I2cBus::recoveryDue(xTaskGetTickCount())) && I2cBus::lock(100)) {
                BatteryProbe probe;
                bool sampled_battery = false;
                // Recheck after acquiring the shared lock: mount/exit may have
                // changed phase while this task waited. Never recover the PMU
                // or touch sensors during the proven boot/handover sequence.
                if (Crabit::amebaTransitionActive() || Crabit::CompanionBle::hardwareTransitioning()) {
                    I2cBus::unlock();
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
                const bool allow_recovery = !Crabit::amebaSessionActive() &&
                    !Crabit::CompanionBle::hardwareActive();
#if CRABIT_QMI_RUN_PROBE
                // UI changes this flag under this same mutex. Do not read or
                // recover sensors while WoM owns the QMI register interface.
                if (QmiRunProbe::active.load(std::memory_order_relaxed)) {
                    I2cBus::unlock();
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
#endif
                if (I2cFault::paused.load(std::memory_order_relaxed)) {
                    if (allow_recovery) I2cBus::recover();
                } else if (!I2cBus::ready()) {
                    I2cFault::fault(I2cFault::Bus, 0);
                    I2cBus::recoveryFault(xTaskGetTickCount());
                } else {
                    uint32_t tick_start = HAL_GetTick();
                    NanoUI::SensorManager::getInstance().tick(tick_start);
                    PublishSensorSnapshot(tick_start);
                    uint32_t tick_cost = HAL_GetTick() - tick_start;
                    WakeMetrics::maximum(WakeMetrics::sensor_max, tick_cost);
                    if (I2cFault::paused.load(std::memory_order_relaxed) || I2cBus::errorCode() != 0) {
                        if (!I2cFault::paused.load(std::memory_order_relaxed)) I2cFault::fault(I2cFault::Bus, 0);
                        I2cBus::recoveryFault(xTaskGetTickCount());
                    } else {
                        // Slow-but-successful polling is not evidence of a broken bus.
                        I2cBus::recoveryHealthy(xTaskGetTickCount());
                        if (I2cBus::recoveryAttempts() == 0)
                            I2cFault::latched.store(false, std::memory_order_relaxed);
                        if (allow_recovery && SysPower::isActive() &&
                            !AuxStop2::restoreFault.load(std::memory_order_relaxed) &&
                            Usb::configured() && tick_start - last_battery_probe >= 5000u) {
                            last_battery_probe = tick_start;
                            probe.read();
                            sampled_battery = true;
                        }
                    }
                }
                I2cBus::unlock();
                if (sampled_battery) probe.log(last_battery_probe);
            }
            
            if (HAL_GetTick() - last_log_tick > 2000) {
                last_log_tick = HAL_GetTick();
                
                size_t free_heap = xPortGetFreeHeapSize();
                size_t min_heap  = xPortGetMinimumEverFreeHeapSize();
                // 按当前 FreeRTOS 配置计算已用堆空间。
                size_t total_heap = configTOTAL_HEAP_SIZE;
                size_t used_heap = (total_heap > free_heap) ? (total_heap - free_heap) : 0;

                size_t ui_w = NanoUI::MemoryPool::instance().getWidgetUsage();
                size_t ui_r = NanoUI::MemoryPool::instance().getRenderUsage();
                size_t ui_v = NanoUI::MemoryPool::instance().getVectorUsage();

                // 输出运行诊断信息。
                USB_LOG("📊 [MEM] Used:%uK/Free:%uK | UI(W+R+V):%uB | MinFree:%uK\r\n",
                        (unsigned int)(used_heap / 1024),
                        (unsigned int)(free_heap / 1024), 
                        (unsigned int)(ui_w + ui_r + ui_v),
                        (unsigned int)(min_heap / 1024));
            }
        } else if (SysPower::canPollGesture() && I2cBus::lock(20)) {
            // Screen-only sleep keeps QMI powered. Poll only this sensor;
            // never run bus/PMU recovery or touch a BW16 transition here.
            if (SysPower::canPollGesture() && !Crabit::amebaSessionActive() &&
                    !Crabit::CompanionBle::hardwareTransitioning() &&
                    I2cBus::ready()) {
                const uint32_t now = HAL_GetTick();
                if (imu_drv.checkTick(now)) {
                    imu_drv.update();
                    PublishSensorSnapshot(now);
                }
            }
            I2cBus::unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }
}


void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    bool active = SysPower::isActive();
    if (GPIO_Pin == GPIO_PIN_1) { g_qmi_int1_cnt++; }
    else if (GPIO_Pin == GPIO_PIN_0) { g_qmi_int2_cnt++; }
    else if (GPIO_Pin == GPIO_PIN_8) { g_pat_int_cnt++; if (active) SysPower::feed(); }
    if (GPIO_Pin == GPIO_PIN_6) { SysPower::feed(); }
    else if (active && (GPIO_Pin == GPIO_PIN_3 || GPIO_Pin == GPIO_PIN_7)) { SysPower::feed(); }
}

