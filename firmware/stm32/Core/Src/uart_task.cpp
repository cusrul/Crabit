// UART modem: DMA ring, frame stitching, TX worker and UART callbacks.
// Bodies moved verbatim from modem_service.cpp / hal_callbacks.cpp.
#include "uart_task.h"
#include "health.h"
#include "topics.h"
#include "uart_owner.h"
#include "ameba_session.h"
#include "NanoUI/NanoUI.h"
#include "event_bus.h"
#include "link_queue.h"
#include "companion_ble.h"
#include "companion_trace.h"
#include "game_tone_service.h"
#include "../../../shared/companion_probe.h"
#include "usb.h"

#include "main.h"
#include "usart.h"

#include <atomic>

#define RX_BUF_LEN 512
struct RxQueueMsg {
    uint8_t idx;
    uint16_t len;
};

static volatile uint32_t dbg_rx_evts = 0;
static volatile uint32_t dbg_rx_errs = 0;
static std::atomic<uint32_t> trace_frames{0}, trace_bad_frames{0}, trace_ui_drops{0};
static uint8_t rx_pingpong[2][RX_BUF_LEN];
static volatile uint8_t rx_write_idx = 0;
static QueueHandle_t rx_msg_queue = NULL;
static SemaphoreHandle_t tx_dma_sem = NULL;

namespace {
constexpr uint32_t kProductBaud = 460800;
constexpr uint32_t kTxDrainTimeoutMs = 250;

std::atomic<uint8_t> owner_mode{static_cast<uint8_t>(UART_OWNER_NORMAL)};
std::atomic<uint32_t> host_baud{0};
std::atomic<uint32_t> pending_baud{0};
std::atomic<uint32_t> active_baud{kProductBaud};
uint8_t uart_irq_was_enabled = 0;
uint8_t rx_dma_irq_was_enabled = 0;

bool txDmaBusy() {
    return huart1.gState == HAL_UART_STATE_BUSY_TX ||
           huart1.gState == HAL_UART_STATE_BUSY_TX_RX;
}

bool abortActiveTx() {
    if (!txDmaBusy()) return true;
    if (HAL_UART_AbortTransmit(&huart1) == HAL_OK) return true;
    // Completion may have won the race with the abort request.
    return !txDmaBusy();
}

bool abortActiveRx() {
    // A deinitialized UART has no clock and no armed stream; never poke its
    // registers from here (BYPASS leaves the handle in RESET on purpose).
    if (huart1.RxState == HAL_UART_STATE_READY ||
        huart1.RxState == HAL_UART_STATE_RESET)
        return true;
    if (HAL_UART_AbortReceive(&huart1) == HAL_OK) return true;
    // Abort reports an error when the transfer already ended.
    return huart1.RxState == HAL_UART_STATE_READY;
}

void clearUartErrorFlags() {
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_FEFLAG(&huart1);
    __HAL_UART_CLEAR_NEFLAG(&huart1);
    __HAL_UART_CLEAR_PEFLAG(&huart1);
}

void drainRxMessages() {
    if (rx_msg_queue == NULL) return;
    RxQueueMsg message{};
    while (xQueueReceive(rx_msg_queue, &message, 0) == pdPASS) {
    }
}

void drainTxMessages() {
    CrabitWire::Message message{};
    while (LinkQueue::receiveTx(message, 0)) {
    }
}
}  // namespace

bool Uart::init() {
    rx_msg_queue = xQueueCreate(4, sizeof(RxQueueMsg));
    tx_dma_sem = xSemaphoreCreateBinary();
    owner_mode.store(static_cast<uint8_t>(UART_OWNER_NORMAL), std::memory_order_release);
    active_baud.store(huart1.Init.BaudRate, std::memory_order_relaxed);
    return rx_msg_queue != NULL && tx_dma_sem != NULL;
}

bool Uart::suspend() {
    if (!UartOwner_Normal()) return false;
    return HAL_UART_DeInit(&huart1) == HAL_OK;
}

bool Uart::resume() {
    if (!UartOwner_Normal()) return false;
    MX_USART1_UART_Init();
    STM32_RestartUartRx();
    UartOwner_SetLineCodingBaud(huart1.Init.BaudRate);
    return huart1.gState == HAL_UART_STATE_READY;
}

bool Uart::ready() {
    return UartOwner_Normal() && huart1.gState == HAL_UART_STATE_READY;
}

bool Uart::configure() {
    if (!UartOwner_Normal()) return false;
    MX_USART1_UART_Init();
    UartOwner_SetLineCodingBaud(huart1.Init.BaudRate);
    return ready();
}

bool UartOwner_Claim(UartOwnerMode mode) {
    if (mode != UART_OWNER_FLASH && mode != UART_OWNER_BYPASS) return false;
    if (rx_msg_queue == NULL || tx_dma_sem == NULL) return false;

    // The gate transition and a normal TX start share one critical section, so
    // no frame can begin after the claim has drained the in-flight transfer.
    taskENTER_CRITICAL();
    if (owner_mode.load(std::memory_order_relaxed) !=
        static_cast<uint8_t>(UART_OWNER_NORMAL)) {
        taskEXIT_CRITICAL();
        return false;
    }
    owner_mode.store(static_cast<uint8_t>(mode), std::memory_order_release);
    taskEXIT_CRITICAL();

    if (mode == UART_OWNER_FLASH) {
        const uint32_t requested = host_baud.load(std::memory_order_relaxed);
        if (requested != 0) pending_baud.store(requested, std::memory_order_release);
    }
    active_baud.store(huart1.Init.BaudRate, std::memory_order_relaxed);

    (void)xSemaphoreTake(tx_dma_sem, 0);
    if (txDmaBusy() &&
        xSemaphoreTake(tx_dma_sem, pdMS_TO_TICKS(kTxDrainTimeoutMs)) != pdTRUE) {
        if (!abortActiveTx()) {
            owner_mode.store(static_cast<uint8_t>(UART_OWNER_FAULT),
                             std::memory_order_release);
            return false;
        }
    }

    uart_irq_was_enabled =
        static_cast<uint8_t>(NVIC_GetEnableIRQ(USART1_IRQn) != 0);
    rx_dma_irq_was_enabled =
        static_cast<uint8_t>(NVIC_GetEnableIRQ(DMA2_Channel7_IRQn) != 0);
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    HAL_NVIC_DisableIRQ(DMA2_Channel7_IRQn);

    // AbortReceive clears TOIDLE, but a previous normal/standard configuration
    // may still have IDLE armed. Quiesce it before touching the DMA stream.
    const bool clocked = huart1.gState != HAL_UART_STATE_RESET;
    if (clocked) ATOMIC_CLEAR_BIT(huart1.Instance->CR1, USART_CR1_IDLEIE);
    if (!abortActiveRx()) {
        owner_mode.store(static_cast<uint8_t>(UART_OWNER_FAULT),
                         std::memory_order_release);
        return false;
    }
    if (clocked) clearUartErrorFlags();
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_ClearPendingIRQ(DMA2_Channel7_IRQn);
    drainRxMessages();
    drainTxMessages();
    rx_write_idx = 0;
    return true;
}

bool UartOwner_Release(UartOwnerMode mode) {
    if (mode != UART_OWNER_FLASH && mode != UART_OWNER_BYPASS) return false;
    if (owner_mode.load(std::memory_order_acquire) != static_cast<uint8_t>(mode))
        return false;

    // The owner stays exclusive until the normal RX path is armed again.
    const bool clocked = huart1.gState != HAL_UART_STATE_RESET;
    if (clocked) ATOMIC_CLEAR_BIT(huart1.Instance->CR1, USART_CR1_IDLEIE);
    bool ok = abortActiveRx();
    ok = abortActiveTx() && ok;
    if (clocked) clearUartErrorFlags();

    if (ok && clocked) ok = HAL_UART_DeInit(&huart1) == HAL_OK;
    if (ok) {
        huart1.Init.BaudRate = kProductBaud;
        ok = HAL_UART_Init(&huart1) == HAL_OK;
    }
    if (ok) {
        // MspInit re-enables USART1_IRQn. Keep both normal RX IRQs masked while
        // the DMA is armed so no callback can observe a half-restored owner.
        HAL_NVIC_DisableIRQ(USART1_IRQn);
        HAL_NVIC_DisableIRQ(DMA2_Channel7_IRQn);
        clearUartErrorFlags();
        rx_write_idx = 0;
        ok = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_pingpong[rx_write_idx],
                                          RX_BUF_LEN - 1) == HAL_OK;
        if (ok) __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    }
    if (!ok) {
        // Keep normal traffic closed rather than exposing a half-restored UART.
        HAL_NVIC_DisableIRQ(USART1_IRQn);
        HAL_NVIC_DisableIRQ(DMA2_Channel7_IRQn);
        (void)abortActiveRx();
        owner_mode.store(static_cast<uint8_t>(UART_OWNER_FAULT),
                         std::memory_order_release);
        return false;
    }

    drainRxMessages();
    drainTxMessages();
    if (tx_dma_sem != NULL) {
        while (xSemaphoreTake(tx_dma_sem, 0) == pdTRUE) {
        }
    }
    pending_baud.store(0, std::memory_order_release);
    active_baud.store(huart1.Init.BaudRate, std::memory_order_relaxed);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_ClearPendingIRQ(DMA2_Channel7_IRQn);

    // The DMA is armed and the owner is normal before any normal IRQ can run;
    // normal tasks/callbacks are the last thing reopened.
    owner_mode.store(static_cast<uint8_t>(UART_OWNER_NORMAL),
                     std::memory_order_release);
    if (uart_irq_was_enabled)
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    else
        HAL_NVIC_DisableIRQ(USART1_IRQn);
    if (rx_dma_irq_was_enabled)
        HAL_NVIC_EnableIRQ(DMA2_Channel7_IRQn);
    else
        HAL_NVIC_DisableIRQ(DMA2_Channel7_IRQn);
    return true;
}

UartOwnerMode UartOwner_Active(void) {
    return static_cast<UartOwnerMode>(
        owner_mode.load(std::memory_order_acquire));
}

bool UartOwner_Normal(void) {
    return UartOwner_Active() == UART_OWNER_NORMAL;
}

bool UartOwner_RequestLineCoding(uint32_t baudrate) {
    if (baudrate == 0) return false;
    host_baud.store(baudrate, std::memory_order_relaxed);
    if (UartOwner_Active() == UART_OWNER_FLASH)
        pending_baud.store(baudrate, std::memory_order_release);
    return true;
}

bool UartOwner_TakePendingLineCoding(uint32_t* baudrate) {
    if (baudrate == NULL) return false;
    if (UartOwner_Active() != UART_OWNER_FLASH) return false;
    const uint32_t value = pending_baud.exchange(0, std::memory_order_acq_rel);
    if (value == 0) return false;
    *baudrate = value;
    return true;
}

uint32_t UartOwner_LineCodingBaud(void) {
    if (UartOwner_Active() == UART_OWNER_FLASH)
        return active_baud.load(std::memory_order_relaxed);
    return huart1.Init.BaudRate;
}

void UartOwner_SetLineCodingBaud(uint32_t baudrate) {
    if (baudrate != 0)
        active_baud.store(baudrate, std::memory_order_relaxed);
}

bool STM32_PrepareAmebaUartRx(void) {
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

void STM32_RestartUartRx(void) {
    HAL_UART_AbortReceive(&huart1);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_pingpong[rx_write_idx], RX_BUF_LEN - 1);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
}

void Task_Modem(void *pvParameters) {
    (void)pvParameters;
    RxQueueMsg msg;
    static uint8_t rx_stitch_buf[CrabitWire::FRAME_SIZE];
    static uint16_t stitch_idx = 0;
    static uint8_t decoded[1024]; 

    while (1) {
        Health::beat(Health::Task::Uart);
        if (xQueueReceive(rx_msg_queue, &msg, pdMS_TO_TICKS(50)) == pdPASS) {
            // A normal RX event queued before an exclusive claim must never be
            // consumed as product traffic by a later owner.
            if (!UartOwner_Normal()) continue;
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
                                // Game tones are consumed here, on the UART
                                // task, so a BW16/coproc game never depends on
                                // the UI EventBus being drained. The driver
                                // remains the only PWM owner.
                                if (strcmp(message.topic, Topics::AppTone) == 0) {
                                    CrabitTone::handleFrame(
                                        message.value, message.value_len, message.request_id,
                                        Crabit::amebaSessionActive() || g_coproc_active,
                                        HAL_GetTick());
                                } else if (strcmp(message.topic, Topics::DiagRadio) != 0) {
                                    // Telemetry terminates here, never in the tiny
                                    // UI/control queue where it could displace READY.
                                    const bool timeFrame = !strcmp(message.topic, Topics::NetTime);
                                    if (timeFrame) Crabit::CompanionTrace::record(
                                        Crabit::CompanionTrace::TimeRx, message.request_id, message.value_len,
                                        CrabitWire::crc16(reinterpret_cast<const uint8_t*>(message.value), message.value_len));
                                    const bool queued = LinkQueue::enqueueUi(message);
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

bool Uart::send(const uint8_t* data, size_t length) {
    HAL_StatusTypeDef status;
    taskENTER_CRITICAL();
    if (!UartOwner_Normal()) {
        taskEXIT_CRITICAL();
        return false;
    }
    status = HAL_UART_Transmit_DMA(&huart1, const_cast<uint8_t*>(data),
                                   static_cast<uint16_t>(length));
    taskEXIT_CRITICAL();
    if (status != HAL_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(2));
    return xSemaphoreTake(tx_dma_sem, pdMS_TO_TICKS(100)) == pdTRUE;
}

void Task_TX_Worker(void *pvParameters) {
    (void)pvParameters;
    if (!LinkQueue::ready() || tx_dma_sem == NULL) Fault::reset(Fault::Reason::Task);

    CrabitWire::Message message;
    uint8_t payload[CrabitWire::PAYLOAD_SIZE];
    uint8_t frame[CrabitWire::FRAME_SIZE];
    
    while (1) {
        if (LinkQueue::receiveTx(message, portMAX_DELAY)) {
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
            
            const bool mode_control = strcmp(message.topic, Topics::SysMode) == 0;
            const bool trace_control = mode_control || strcmp(message.topic, Topics::SysComm) == 0 || strcmp(message.topic, Topics::SysBwPing) == 0;
            const uint32_t mode_code = strcmp(message.value, "UI") == 0 ? 0u :
                strcmp(message.value, "BLE") == 0 ? 1u : strcmp(message.value, "WIFI") == 0 ? 2u : 3u;
            if (trace_control) Crabit::CompanionTrace::record(mode_control ? Crabit::CompanionTrace::ModeTxBegin : Crabit::CompanionTrace::TxBegin,
                message.request_id, mode_control ? mode_code : frame_len, huart1.gState);
            HAL_StatusTypeDef tx_result;
            taskENTER_CRITICAL();
            if (UartOwner_Normal()) {
                tx_result = HAL_UART_Transmit_DMA(&huart1, frame, frame_len);
            } else {
                // Exclusive owner or fault: drop the stale queued frame instead
                // of injecting it into a bridge/bypassed UART.
                tx_result = HAL_BUSY;
            }
            taskEXIT_CRITICAL();
            if (tx_result != HAL_OK) {
                if (trace_control) Crabit::CompanionTrace::record(mode_control ? Crabit::CompanionTrace::ModeTxEnd : Crabit::CompanionTrace::TxEnd,
                    message.request_id, tx_result, 0);
                if (UartOwner_Normal())
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


bool Uart::startRx() {
    if (!UartOwner_Normal()) return false;
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_pingpong[rx_write_idx],
                                     RX_BUF_LEN - 1) != HAL_OK)
        return false;
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    return true;
}

bool Uart::prepareAmebaRx() {
    if (!UartOwner_Normal()) return false;
    return STM32_PrepareAmebaUartRx();
}

uint32_t Uart::rxEvents() { return dbg_rx_evts; }
uint32_t Uart::rxErrors() { return dbg_rx_errs; }
uint32_t Uart::frames() { return trace_frames.load(); }
uint32_t Uart::badFrames() { return trace_bad_frames.load(); }
uint32_t Uart::uiDrops() { return trace_ui_drops.load(); }

void Modem_StartRx(void) {
    (void)Uart::startRx();
}
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    // Never rotate or re-arm the normal ping-pong DMA while a bridge or bypass
    // owner controls USART1, even if a stale interrupt was already pending.
    if (huart->Instance == USART1 && UartOwner_Normal()) {
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

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (tx_dma_sem != NULL) {
            xSemaphoreGiveFromISR(tx_dma_sem, &xHigherPriorityTaskWoken);
        }
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1 && UartOwner_Normal()) {
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

