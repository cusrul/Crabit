#define SYS_USB_MODE 1 

#include "NanoUI/NanoUI.h"
#include "app_real/launcher.h" 
#include "app_real/app_clock.h" 
#include "sys_config.h" 
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h" 
#include "semphr.h"       
#include <math.h>
#include <stdarg.h> 
#include "usart.h" 


#include "hw_display.h"
#include "hw_input.h"
#include "hw_sensors.h"
#include "global_store.h"
#include "event_bus.h"
#include "sys_services.h" 
#include "sys_power.h"    

#include "badusb_executor.h"
#include "axp2101.h"
#include "hw_ws2812.h"
#include "i2c.h"
#include "sensor_snapshot.h"
#include "ameba_session.h"
#include "companion_ble.h"
#include "companion_trace.h"
#include "i2c_recovery_policy.h"
#include "../../../shared/companion_probe.h"
#if CRABIT_QMI_RUN_PROBE
#include "qmi_run_probe.h"
#include "app_real/app_qmi_run_probe.h"
#endif

#if SYS_USB_MODE == 1
#include "usbd_cdc_if.h" 
#include "usb_diag.h"
#endif

#include "hw_display.h"
#include "hw_input.h"
#include "NanoUI/core/memory_pool.h" 

extern "C" void MX_USART1_UART_Init(void);
extern QMI8658_Driver imu_drv;
extern Battery_Driver batt_drv;
extern PAT9125_Driver pat_drv;
extern Buzzer_Driver buzzer_drv;
extern BMP280_Driver bmp_drv;
extern AXP2101_Driver axp_drv;
extern volatile bool g_coproc_active;

static SemaphoreHandle_t usb_log_mutex = NULL;
static SemaphoreHandle_t i2c_bus_mutex = NULL;
std::atomic<Crabit::AmebaPhase> Crabit::ameba_phase{Crabit::AmebaPhase::Inactive};
Crabit::SnapshotStore<StmSensorSnapshot> g_stm_sensor_snapshot;

bool STM32_ReadSensorSnapshot(StmSensorSnapshot* out) {
    return g_stm_sensor_snapshot.read(out);
}

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

extern "C" bool STM32_I2C_Lock(uint32_t timeout_ms) {
    if (i2c_bus_mutex == NULL) return true;
    return xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

extern "C" void STM32_I2C_Unlock(void) {
    if (i2c_bus_mutex != NULL) xSemaphoreGive(i2c_bus_mutex);
}

static I2cRecovery::Budget sensor_recovery(pdMS_TO_TICKS(2000));

struct SensorBusPins {
    bool scl() const { return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET; }
    bool sda() const { return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == GPIO_PIN_SET; }
    void pause() const { for (unsigned i = 0; i < 800; ++i) __NOP(); }
    bool waitScl() const {
        for (unsigned i = 0; i < 16; ++i) { if (scl()) return true; pause(); }
        return scl();
    }
    void sclLow() { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET); }
    void sclRelease() { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET); }
    void sdaLow() { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET); }
    void sdaRelease() { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET); }
    void releaseOutputs() {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_SET);
    }
    void gpioOpenDrain() {
        releaseOutputs(); // Set release latches BEFORE changing modes; never push-pull high.
        GPIO_InitTypeDef pins = {};
        pins.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        pins.Mode = GPIO_MODE_OUTPUT_OD; pins.Pull = GPIO_PULLUP;
        pins.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &pins);
    }
};

// Called only by SensorHub while it owns i2c_bus_mutex. No PMIC or sensor writes.
static bool RecoverSensorBus() {
    sensor_recovery.beginAttempt(xTaskGetTickCount());
    I2cDiag::attempts.store(sensor_recovery.attempts(), std::memory_order_relaxed);
    WakeMetrics::recoveries.fetch_add(1, std::memory_order_relaxed);
    if (HAL_I2C_DeInit(&hi2c1) != HAL_OK) {
        I2cDiag::result.store(uint32_t(I2cRecovery::Result::InitFailed), std::memory_order_relaxed);
        return false;
    }
    GPIO_InitTypeDef pins = {};
    pins.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    pins.Mode = GPIO_MODE_INPUT; pins.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &pins);
    SensorBusPins bus;
    bus.pause();
    I2cRecovery::Result result = I2cRecovery::Result::Idle;
    if (!bus.scl()) result = I2cRecovery::Result::SclLow;
    else if (!bus.sda()) {
        result = sensor_recovery.consumeClear() ? I2cRecovery::releaseBus(bus) : I2cRecovery::Result::SdaLow;
    }
    // Always restore the original PA9/PA10 AF4 configuration, including on failure.
    MX_I2C1_Init();
    if (result == I2cRecovery::Result::Idle || result == I2cRecovery::Result::Released) {
        uint8_t id = 0;
        const uint32_t failed_addr = I2cDiag::address.load(std::memory_order_relaxed) << 1;
        // Read-only probe; ACK/readback is not a claim that all sensor configuration survived.
        if (HAL_I2C_Mem_Read(&hi2c1, AXP2101_ADDR, AXP2101_IC_TYPE, 1, &id, 1, 5) != HAL_OK ||
            id != 0x4A || (failed_addr &&
            HAL_I2C_IsDeviceReady(&hi2c1, failed_addr, 1, 5) != HAL_OK) ||
            !bus.scl() || !bus.sda()) result = I2cRecovery::Result::ProbeFailed;
    }
    I2cDiag::result.store(uint32_t(result), std::memory_order_relaxed);
    const bool passed = result == I2cRecovery::Result::Idle || result == I2cRecovery::Result::Released;
    if (passed) {
        sensor_recovery.probePassed(xTaskGetTickCount());
        I2cDiag::paused.store(false, std::memory_order_relaxed);
    }
    return passed;
}

// USB 日志采用有界等待：互斥锁等待和发送重试可能阻塞调用任务。
extern "C" void USB_LOG(const char* format, ...) {
#if SYS_USB_MODE == 1
    static char loc_buf[256];
    const bool in_isr = (__get_IPSR() != 0);
    if (in_isr) return;
    char* buffer = loc_buf;
    const size_t buffer_size = sizeof(loc_buf);
    bool locked = false;
    if (usb_log_mutex != NULL) {
        locked = xSemaphoreTake(usb_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
        if (!locked) return;
    }
    va_list arg;
    va_start(arg, format);
    int len = vsnprintf(buffer, buffer_size, format, arg);
    va_end(arg);

    if (len >= (int)buffer_size) len = (int)buffer_size - 1;
    if (len > 0) {
        uint8_t retries = 5;
        while(CDC_Transmit_FS((uint8_t*)buffer, (uint16_t)len) == USBD_BUSY && retries--) {
            vTaskDelay(1);
        }
    }
    if (locked) xSemaphoreGive(usb_log_mutex);
#else
    (void)format; 
#endif
}



// 全局变量声明
GlobalDataStore GlobalStore = {0};
volatile uint32_t dbg_rx_evts = 0; 
volatile uint32_t dbg_rx_errs = 0; 
static std::atomic<uint32_t> trace_frames{0}, trace_bad_frames{0}, trace_ui_drops{0};
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

// DMA 接收队列
#define RX_BUF_LEN 512
uint8_t rx_pingpong[2][RX_BUF_LEN]; 
volatile uint8_t rx_write_idx = 0;  
struct RxQueueMsg { uint8_t idx; uint16_t len; };
QueueHandle_t rx_msg_queue = NULL;  
SemaphoreHandle_t tx_dma_sem = NULL; 

extern "C" bool STM32_PrepareAmebaUartRx(void) {
    // Boot temporarily uses PB7 as a pulled-up GPIO. Re-initializing an already
    // initialized UART neither reruns MSP pin setup nor preserves TOIDLE mode.
    // Mask UART-related IRQs only. TX DMA error callbacks also restart RX,
    // so they must not re-enter this receive setup. The DMA itself keeps running.
    const uint32_t uart_irq_enabled = NVIC_GetEnableIRQ(USART1_IRQn);
    const uint32_t dma_irq_enabled = NVIC_GetEnableIRQ(DMA2_Channel7_IRQn);
    const uint32_t tx_dma_irq_enabled = NVIC_GetEnableIRQ(DMA1_Channel4_IRQn);
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_DisableIRQ(DMA2_Channel7_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Channel4_IRQn);

    // AbortReceive only clears IDLEIE for TOIDLE, but normal boot changed it
    // to STANDARD. Explicitly quiesce the old IDLE source before aborting.
    ATOMIC_CLEAR_BIT(huart1.Instance->CR1, USART_CR1_IDLEIE);
    bool ready = HAL_UART_AbortReceive(&huart1) == HAL_OK;
    if (ready) {
        GPIO_InitTypeDef pin = {};
        pin.Pin = GPIO_PIN_7;
        pin.Mode = GPIO_MODE_AF_PP;
        pin.Pull = GPIO_NOPULL;
        pin.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        pin.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOB, &pin);
        ready = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_pingpong[rx_write_idx],
                                            RX_BUF_LEN - 1) == HAL_OK;
        if (ready) __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    }
    // Do not clear pending USART IRQs: a concurrent TX completion belongs to
    // the existing TX worker. Re-enable only IRQs enabled on entry.
    if (tx_dma_irq_enabled) HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    if (dma_irq_enabled) HAL_NVIC_EnableIRQ(DMA2_Channel7_IRQn);
    if (uart_irq_enabled) HAL_NVIC_EnableIRQ(USART1_IRQn);
    return ready;
}

extern "C" void STM32_RestartUartRx(void) {
    HAL_UART_AbortReceive(&huart1);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_pingpong[rx_write_idx], RX_BUF_LEN - 1);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
}

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART1) {
        dbg_rx_evts++; 
        const auto event_type = HAL_UARTEx_GetRxEventType(huart);
        const uint32_t remaining = __HAL_DMA_GET_COUNTER(huart->hdmarx);
        if (Crabit::CompanionBle::hardwareActive()) {
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::RxEvent, 0,
                Size, event_type);
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::RxDma, 0,
                rx_write_idx, remaining);
        }
        // Circular HAL reports full-size IDLE even when freshly armed DMA has
        // received nothing. A genuine full buffer with TC pending is preserved.
        // Read remaining before TC so completion between reads is not discarded.
        if (event_type == HAL_UART_RXEVENT_IDLE && Size != 0 &&
                Size == huart->RxXferSize && remaining == huart->RxXferSize &&
                HAL_IS_BIT_SET(huart->hdmarx->Instance->CCR, DMA_CCR_CIRC) &&
                __HAL_DMA_GET_FLAG(huart->hdmarx, __HAL_DMA_GET_TC_FLAG_INDEX(huart->hdmarx)) == RESET) {
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::RxEmptyIdle, 0, Size, remaining);
            return; // No queue entry, buffer rotation or DMA restart.
        }
        if (Size >= RX_BUF_LEN) Size = RX_BUF_LEN - 1;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        RxQueueMsg msg = {rx_write_idx, Size}; 
        if (rx_msg_queue != NULL) {
            if (xQueueSendFromISR(rx_msg_queue, &msg, &xHigherPriorityTaskWoken) != pdPASS) {
                dbg_rx_errs++;
            }
        }
        rx_write_idx = (rx_write_idx + 1) % 2;
        STM32_RestartUartRx();
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (tx_dma_sem != NULL) {
            xSemaphoreGiveFromISR(tx_dma_sem, &xHigherPriorityTaskWoken);
        }
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        dbg_rx_errs++; 
        Crabit::CompanionTrace::record(Crabit::CompanionTrace::UartError, 0,
            huart->ErrorCode, huart->Instance->ISR);
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_PEFLAG(huart);
        STM32_RestartUartRx();
    }
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    bool active = SysPower::isActive();
    if (GPIO_Pin == GPIO_PIN_1) { g_qmi_int1_cnt++; }
    else if (GPIO_Pin == GPIO_PIN_0) { g_qmi_int2_cnt++; }
    else if (GPIO_Pin == GPIO_PIN_8) { g_pat_int_cnt++; if (active) SysPower::feed(); }
    if (GPIO_Pin == GPIO_PIN_6) { SysPower::feed(); }
    else if (active && (GPIO_Pin == GPIO_PIN_3 || GPIO_Pin == GPIO_PIN_7)) { SysPower::feed(); }
}

extern "C" void OLED_Init(void); 
void OLED_SetSleep(bool sleep) {
    if (sleep) { OLED_WR_Byte(0xAE, 0); OLED_WR_Byte(0x8D, 0); OLED_WR_Byte(0x10, 0); } 
    else { OLED_WR_Byte(0x8D, 0); OLED_WR_Byte(0x14, 0); OLED_WR_Byte(0xAF, 0); }
}

void Task_Modem(void *pvParameters) {
    (void)pvParameters;
    RxQueueMsg msg;
    static uint8_t rx_stitch_buf[CrabitWire::FRAME_SIZE];
    static uint16_t stitch_idx = 0;
    static uint8_t decoded[1024]; 

    while (1) {
        if (xQueueReceive(rx_msg_queue, &msg, pdMS_TO_TICKS(50)) == pdPASS) {
            GlobalStore.ble_client_count++; 
            uint8_t* raw_data = rx_pingpong[msg.idx];
            uint16_t total_len = msg.len;
            if (Crabit::CompanionBle::hardwareActive()) {
                const auto bytes = Crabit::CompanionProbe::summarize(raw_data, total_len);
                Crabit::CompanionTrace::record(Crabit::CompanionTrace::RxBytes, 0, bytes.size, bytes.zero);
                Crabit::CompanionTrace::record(Crabit::CompanionTrace::RxClasses, 0, bytes.ff, bytes.printable);
            }
            
            for (uint16_t i = 0; i < total_len; i++) {
                // A partial candidate always leaves room for the next byte.
                rx_stitch_buf[stitch_idx++] = raw_data[i];
                while (stitch_idx >= 2) {
                    const uint16_t declared = (uint16_t)rx_stitch_buf[0] |
                                              ((uint16_t)rx_stitch_buf[1] << 8);
                    const size_t expected = (size_t)declared + 4U;
                    // Serial also carries SDK boot text. Reject impossible
                    // candidates early, but retain the next byte as a possible
                    // length prefix rather than losing alignment in pairs.
                    bool header_valid = declared >= 5 && expected <= sizeof(rx_stitch_buf);
                    if (stitch_idx >= 3 && rx_stitch_buf[2] != CrabitWire::VERSION) {
                        header_valid = false;
                    }
                    if (stitch_idx >= 4 && rx_stitch_buf[3] > CrabitWire::MAX_TOPIC) {
                        header_valid = false;
                    }
                    if (stitch_idx >= 5 &&
                        (rx_stitch_buf[4] > CrabitWire::MAX_VALUE ||
                         declared != 5U + rx_stitch_buf[3] + rx_stitch_buf[4])) {
                        header_valid = false;
                    }
                    size_t consumed = 1;
                    if (!header_valid) {
                        ++trace_bad_frames;
                    } else {
                        if (stitch_idx < expected) break; // Preserve a split frame.
                        size_t payload_len = 0;
                        if (CrabitWire::decode_frame(rx_stitch_buf, expected, decoded,
                                                     CrabitWire::PAYLOAD_SIZE, &payload_len)) {
                            CrabitWire::Message message;
                            if (CrabitWire::decode_message(decoded, payload_len, &message)) {
                                consumed = expected;
                                ++trace_frames;
                                Crabit::CompanionTrace::message(message.topic, message.value, message.request_id);
                                // Telemetry terminates here, never in the tiny
                                // UI/control queue where it could displace READY.
                                if (strcmp(message.topic, "diag/radio") != 0) {
                                    const bool timeFrame = !strcmp(message.topic, "net/time");
                                    if (timeFrame) Crabit::CompanionTrace::record(
                                        Crabit::CompanionTrace::TimeRx, message.request_id, message.value_len,
                                        CrabitWire::crc16(reinterpret_cast<const uint8_t*>(message.value), message.value_len));
                                    const bool queued = EventBus::enqueueMessage(message);
                                    if (timeFrame) Crabit::CompanionTrace::record(
                                        Crabit::CompanionTrace::TimeQueued, message.request_id, queued ? 1u : 0u);
                                    if (!queued) { ++trace_ui_drops; dbg_rx_errs++; }
                                }
                            } else ++trace_bad_frames;
                        } else { ++trace_bad_frames; dbg_rx_errs++; }
                    }
                    // Bad CRC/header: slide one byte and inspect the retained
                    // suffix. Valid frame: consume only that frame, not its tail.
                    stitch_idx -= consumed;
                    memmove(rx_stitch_buf, rx_stitch_buf + consumed, stitch_idx);
                }
            }
        }
    }
}

void Task_TX_Worker(void *pvParameters) {
    (void)pvParameters;
    if (EventBus::tx_queue == NULL || tx_dma_sem == NULL) { vTaskSuspend(NULL); }

    CrabitWire::Message message;
    uint8_t payload[CrabitWire::PAYLOAD_SIZE];
    uint8_t frame[CrabitWire::FRAME_SIZE];
    
    while (1) {
        if (xQueueReceive(EventBus::tx_queue, &message, portMAX_DELAY) == pdPASS) {
            size_t payload_len = 5 + message.topic_len + message.value_len;
            payload[0] = CrabitWire::VERSION;
            payload[1] = message.topic_len;
            payload[2] = message.value_len;
            payload[3] = (uint8_t)message.request_id;
            payload[4] = (uint8_t)(message.request_id >> 8);
            memcpy(payload + 5, message.topic, message.topic_len);
            memcpy(payload + 5 + message.topic_len, message.value, message.value_len);
            size_t frame_len = 0;
            if (!CrabitWire::encode_frame(payload, payload_len, frame, sizeof(frame), &frame_len)) continue;
            
            const bool mode_control = strcmp(message.topic, "sys/mode") == 0;
            const bool trace_control = mode_control || strcmp(message.topic, "sys/comm") == 0 || strcmp(message.topic, "sys/bw/ping") == 0;
            const uint32_t mode_code = strcmp(message.value, "UI") == 0 ? 0u :
                strcmp(message.value, "BLE") == 0 ? 1u : strcmp(message.value, "WIFI") == 0 ? 2u : 3u;
            if (trace_control) Crabit::CompanionTrace::record(mode_control ? Crabit::CompanionTrace::ModeTxBegin : Crabit::CompanionTrace::TxBegin,
                message.request_id, mode_control ? mode_code : frame_len, huart1.gState);
            const HAL_StatusTypeDef tx_result = HAL_UART_Transmit_DMA(&huart1, frame, frame_len);
            if (tx_result != HAL_OK) {
                if (trace_control) Crabit::CompanionTrace::record(mode_control ? Crabit::CompanionTrace::ModeTxEnd : Crabit::CompanionTrace::TxEnd,
                    message.request_id, tx_result, 0);
                USB_LOG("[UART] TX DMA start failed\r\n");
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            const BaseType_t completed = xSemaphoreTake(tx_dma_sem, pdMS_TO_TICKS(100));
            if (trace_control) Crabit::CompanionTrace::record(mode_control ? Crabit::CompanionTrace::ModeTxEnd : Crabit::CompanionTrace::TxEnd,
                message.request_id, tx_result, completed);
        }
    }
}

void Task_SensorHub(void *pvParameters) {
    (void)pvParameters;
    uint32_t last_log_tick = 0;
    
    while (1) {
        if (SysPower::isActive()) { 
            if (sensor_recovery.syncWake(WakeMetrics::wakes.load(std::memory_order_relaxed))) {
                I2cDiag::paused.store(false, std::memory_order_relaxed);
                I2cDiag::latched.store(false, std::memory_order_relaxed);
                I2cDiag::attempts.store(0, std::memory_order_relaxed);
            }
            if ((!I2cDiag::paused.load(std::memory_order_relaxed) ||
                 sensor_recovery.due(xTaskGetTickCount())) && STM32_I2C_Lock(100)) {
                // Recheck after acquiring the shared lock: mount/exit may have
                // changed phase while this task waited. Never recover the PMU
                // or touch sensors during the proven boot/handover sequence.
                if (Crabit::amebaTransitionActive() || Crabit::CompanionBle::hardwareTransitioning()) {
                    STM32_I2C_Unlock();
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
                const bool allow_recovery = !Crabit::amebaSessionActive() &&
                    !Crabit::CompanionBle::hardwareActive();
#if CRABIT_QMI_RUN_PROBE
                // UI changes this flag under this same mutex. Do not read or
                // recover sensors while WoM owns the QMI register interface.
                if (QmiRunProbe::active.load(std::memory_order_relaxed)) {
                    STM32_I2C_Unlock();
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
#endif
                if (I2cDiag::paused.load(std::memory_order_relaxed)) {
                    if (allow_recovery) RecoverSensorBus();
                } else if (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY) {
                    I2cDiag::fault(I2cDiag::Bus, 0);
                    sensor_recovery.fault(xTaskGetTickCount());
                } else {
                    uint32_t tick_start = HAL_GetTick();
                    NanoUI::SensorManager::getInstance().tick(tick_start);
                    PublishSensorSnapshot(tick_start);
                    uint32_t tick_cost = HAL_GetTick() - tick_start;
                    WakeMetrics::maximum(WakeMetrics::sensor_max, tick_cost);
                    if (I2cDiag::paused.load(std::memory_order_relaxed) || hi2c1.ErrorCode != HAL_I2C_ERROR_NONE) {
                        if (!I2cDiag::paused.load(std::memory_order_relaxed)) I2cDiag::fault(I2cDiag::Bus, 0);
                        sensor_recovery.fault(xTaskGetTickCount());
                    } else {
                        // Slow-but-successful polling is not evidence of a broken bus.
                        sensor_recovery.healthy(xTaskGetTickCount());
                        if (sensor_recovery.attempts() == 0)
                            I2cDiag::latched.store(false, std::memory_order_relaxed);
                    }
                }
                STM32_I2C_Unlock();
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
        } else if (SysPower::canPollGesture() && STM32_I2C_Lock(20)) {
            // Screen-only sleep keeps QMI powered. Poll only this sensor;
            // never run bus/PMU recovery or touch a BW16 transition here.
            if (SysPower::canPollGesture() && !Crabit::amebaSessionActive() &&
                    !Crabit::CompanionBle::hardwareTransitioning() &&
                    HAL_I2C_GetState(&hi2c1) == HAL_I2C_STATE_READY) {
                const uint32_t now = HAL_GetTick();
                if (imu_drv.checkTick(now)) {
                    imu_drv.update();
                    PublishSensorSnapshot(now);
                }
            }
            STM32_I2C_Unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }
}

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

extern "C" void SystemClock_Config(void);


extern "C" void NanoUI_MainLoop(void) {
    usb_log_mutex = xSemaphoreCreateMutex();
    i2c_bus_mutex = xSemaphoreCreateMutex();
    if (usb_log_mutex == NULL || i2c_bus_mutex == NULL) {
        vTaskSuspend(NULL);
        return;
    }

    // 在创建 UI 对象前初始化分配器。
    g_rtos_allocator.init();
    
    // 注册 NanoUI 使用的内存适配器。
    NanoUI::MemoryPool::instance().setAdapter(&g_rtos_allocator);

    ConfigManager::load();
    GlobalStore.sys_volume = CrabitPreferences::current.volume;

    if (AXP2101_Init(&hi2c1) != HAL_OK ||
            AXP2101_SetDC1VoltageChecked(&hi2c1, 3300) != HAL_OK) {
        USB_LOG("[PWR] boot DC1 configuration check failed; UI startup aborted\r\n");
        vTaskSuspend(NULL);
        return;
    }
    HAL_Time::init([]() -> uint32_t { return HAL_GetTick(); }, [](uint32_t ms)  { vTaskDelay(pdMS_TO_TICKS(ms)); });

    // Boot default: keep BW16 powered off until a deliberate wake-up path is chosen.
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(100)); 

    keys.init(); 
   
    imu_drv.init(); batt_drv.init(); pat_drv.init(); buzzer_drv.init();
    USB_LOG("[QMI] tap config=%s; screen-only gesture wake\r\n", imu_drv.tapReady() ? "OK" : "FAILED");
    bmp_drv.init(); axp_drv.init();
    NanoUI::SensorManager::getInstance().registerSensor(&imu_drv);
    NanoUI::SensorManager::getInstance().registerSensor(&batt_drv);
    NanoUI::SensorManager::getInstance().registerSensor(&pat_drv);
    NanoUI::SensorManager::getInstance().registerSensor(&buzzer_drv);
    NanoUI::SensorManager::getInstance().registerSensor(&bmp_drv);
    NanoUI::SensorManager::getInstance().registerSensor(&axp_drv);
   

    AppManager::init(&oled, &keys); 
    Navigator::init(&wm);
    if (!EventBus::init()) {
        USB_LOG("[RTOS] event bus allocation failed\r\n");
        vTaskSuspend(NULL);
        return;
    }
    SysServices::init(&wm); 
    EventBus::setSessionStatusHandler(Crabit::CompanionBle::receive);
    EventBus::subscribe("sys/notify", Crabit::CompanionBle::observeProbePong);
    SysPower::init();       

    OLED_Init(); 
    OLED_SetSleep(false); 
    uint8_t contrast = (uint8_t)(sys_cfg.brightness * 255.0f);
    OLED_WR_Byte(0x81, 0); OLED_WR_Byte(contrast, 0); OLED_WR_Byte(sys_cfg.invert_disp ? 0xA7 : 0xA6, 0);

    rx_msg_queue = xQueueCreate(4, sizeof(RxQueueMsg));
    tx_dma_sem = xSemaphoreCreateBinary();
    if (rx_msg_queue == NULL || tx_dma_sem == NULL) {
        USB_LOG("[RTOS] communication resource allocation failed\r\n");
        vTaskSuspend(NULL);
        return;
    }
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_pingpong[rx_write_idx], RX_BUF_LEN - 1);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT); 

    vTaskDelay(pdMS_TO_TICKS(500)); 
    if (sys_cfg.enable_ble && Crabit::CompanionBle::legacyAutostartAllowed()) {
        USB_LOG("🚀 [SYS] Wake up BW16 -> BLE Mode\r\n");
        BW16_NormalBoot();
        vTaskDelay(pdMS_TO_TICKS(650));
        EventBus::publish("sys/mode", "BLE"); 
        Crabit::CompanionBle::registerLegacyAutostart();
    } else {
        EventBus::publish("sys/mode", "EXIT");
    }

    // Normal product root; STOP2 metrics remain available in System Info.
#if CRABIT_QMI_RUN_PROBE
    AppManager::launch(new QmiRunProbeApp());
#else
    AppManager::launch(new ClockApp());
#endif

    // 创建后台任务；栈参数使用 FreeRTOS 的 StackType_t 单位。
    TaskHandle_t modem_task = NULL;
    TaskHandle_t tx_task = NULL;
    TaskHandle_t sensor_task = NULL;
    BaseType_t tasks_ok = xTaskCreate(Task_Modem, "Modem", 512, NULL, 3, &modem_task);
    tasks_ok &= xTaskCreate(Task_TX_Worker, "TX_Worker", 512, NULL, 3, &tx_task);
    tasks_ok &= xTaskCreate(Task_SensorHub, "Sensors", 1024, NULL, 3, &sensor_task);
    if (tasks_ok != pdPASS) {
        USB_LOG("[RTOS] background task creation failed\r\n");
        if (modem_task) vTaskDelete(modem_task);
        if (tx_task) vTaskDelete(tx_task);
        if (sensor_task) vTaskDelete(sensor_task);
        vTaskSuspend(NULL);
        return;
    }
    // Temporarily disabled while BW16 flashing/CyberLink debugging needs RAM headroom.
    // xTaskCreate(BadUsbExecutor::Task_Detonator, "Detonator", 256, NULL, 3, NULL);

    while(1) {
        CrabitUsb_DiagTick();
        // Feedback expiry belongs to this UI owner even while the screen is off.
        InputFeedback::tick();
        // Observe only; never send a radio command or block on the USB host.
        static uint32_t trace_last = 0, trace_until = 0;
        const uint32_t trace_now = HAL_GetTick();
        if (Crabit::CompanionBle::hardwareActive()) trace_until = trace_now + 10000;
        if (int32_t(trace_until - trace_now) > 0 && uint32_t(trace_now - trace_last) >= 1000) {
            trace_last = trace_now;
            const uint16_t id = Crabit::CompanionBle::sessionId();
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::RxCounts, id, dbg_rx_evts, dbg_rx_errs);
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::FrameCounts, id,
                trace_frames.load(), trace_bad_frames.load());
            Crabit::CompanionTrace::record(Crabit::CompanionTrace::QueueCounts, id,
                trace_ui_drops.load(), uxQueueMessagesWaiting(EventBus::tx_queue));
        }
        Crabit::CompanionTrace::poll();
        const uint32_t lock_started = HAL_GetTick();
        const bool i2c_locked = STM32_I2C_Lock(100);
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
                STM32_I2C_Unlock();
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
            if (i2c_locked) STM32_I2C_Unlock();
            vTaskDelay(pdMS_TO_TICKS(50)); 
        }
    }
}
