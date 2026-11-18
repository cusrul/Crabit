// Host harness for the production UART owner gate. The Python test injects the
// helper namespace and UartOwner_* functions from uart_task.cpp verbatim.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "uart_owner.h"

using BaseType_t = int;
using TickType_t = uint32_t;
using QueueHandle_t = void *;
using SemaphoreHandle_t = struct FakeSem *;
constexpr BaseType_t pdPASS = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdTRUE = 1;
constexpr TickType_t portMAX_DELAY = 0xffffffffu;
constexpr TickType_t pdMS_TO_TICKS(TickType_t ms) { return ms; }
constexpr uint16_t RX_BUF_LEN = 512;
constexpr int HAL_OK = 0;
constexpr int HAL_ERROR = 1;
constexpr int HAL_BUSY = 2;
constexpr uint32_t HAL_UART_STATE_RESET = 0;
constexpr uint32_t HAL_UART_STATE_READY = 1;
constexpr uint32_t HAL_UART_STATE_BUSY_TX = 2;
constexpr uint32_t HAL_UART_STATE_BUSY_RX = 3;
constexpr uint32_t HAL_UART_STATE_BUSY_TX_RX = 4;
constexpr uint32_t USART_CR1_IDLEIE = 1u << 4;
constexpr uint32_t DMA_IT_HT = 1u << 2;
constexpr uint32_t USART1_IRQn = 37;
constexpr uint32_t DMA2_Channel7_IRQn = 70;

struct FakeInstance {
    uint32_t CR1 = USART_CR1_IDLEIE;
};
struct FakeDmaInstance {
    uint32_t dummy = 0;
};
struct DMA_HandleTypeDef {
    FakeDmaInstance *Instance = nullptr;
};
struct UART_InitTypeDef {
    uint32_t BaudRate = 0;
};
struct UART_HandleTypeDef {
    FakeInstance *Instance = nullptr;
    UART_InitTypeDef Init;
    uint32_t gState = HAL_UART_STATE_READY;
    uint32_t RxState = HAL_UART_STATE_READY;
    DMA_HandleTypeDef *hdmarx = nullptr;
};

FakeInstance usart;
FakeDmaInstance dma_instance;
DMA_HandleTypeDef hdma;
UART_HandleTypeDef huart1;

struct RxQueueMsg {
    uint8_t idx;
    uint16_t len;
};
QueueHandle_t rx_msg_queue = reinterpret_cast<QueueHandle_t>(1);
struct FakeSem {
    int count = 0;
};
FakeSem tx_sem;
SemaphoreHandle_t tx_dma_sem = &tx_sem;
uint8_t rx_pingpong[2][RX_BUF_LEN];
volatile uint8_t rx_write_idx = 1;

#define ATOMIC_CLEAR_BIT(reg, bit) ((reg) &= ~(bit))
#define __HAL_DMA_DISABLE_IT(handle, it) ((void)(handle), (void)(it))
#define __HAL_UART_CLEAR_OREFLAG(handle) ((void)(handle))
#define __HAL_UART_CLEAR_FEFLAG(handle) ((void)(handle))
#define __HAL_UART_CLEAR_NEFLAG(handle) ((void)(handle))
#define __HAL_UART_CLEAR_PEFLAG(handle) ((void)(handle))
#define taskENTER_CRITICAL() (void)0
#define taskEXIT_CRITICAL() (void)0

unsigned hal_deinit_calls = 0, hal_init_calls = 0, hal_rx_calls = 0;
unsigned abort_rx_calls = 0, abort_tx_calls = 0, clear_pending_calls = 0;
unsigned uart_irq_enable_calls = 0, dma_irq_enable_calls = 0;
unsigned rx_queue_drains = 0, tx_queue_drains = 0;
bool fail_deinit = false, fail_init = false, fail_rx = false, fail_abort_rx = false,
     fail_abort_tx = false;
bool owner_was_normal_during_rx_arm = false;
uint32_t programmed_baud = 0;

int HAL_UART_DeInit(UART_HandleTypeDef *uart) {
    ++hal_deinit_calls;
    if (fail_deinit) return HAL_ERROR;
    uart->gState = HAL_UART_STATE_RESET;
    uart->RxState = HAL_UART_STATE_RESET;
    return HAL_OK;
}
int HAL_UART_Init(UART_HandleTypeDef *uart) {
    ++hal_init_calls;
    if (fail_init) return HAL_ERROR;
    programmed_baud = uart->Init.BaudRate;
    uart->gState = HAL_UART_STATE_READY;
    return HAL_OK;
}
int HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *uart, uint8_t *, uint16_t) {
    ++hal_rx_calls;
    owner_was_normal_during_rx_arm = UartOwner_Normal();
    if (fail_rx) return HAL_ERROR;
    uart->RxState = HAL_UART_STATE_BUSY_RX;
    return HAL_OK;
}
int HAL_UART_AbortReceive(UART_HandleTypeDef *uart) {
    ++abort_rx_calls;
    if (fail_abort_rx) return HAL_ERROR;
    uart->RxState = HAL_UART_STATE_READY;
    return HAL_OK;
}
int HAL_UART_AbortTransmit(UART_HandleTypeDef *uart) {
    ++abort_tx_calls;
    if (fail_abort_tx) return HAL_ERROR;
    uart->gState = HAL_UART_STATE_READY;
    return HAL_OK;
}
uint32_t NVIC_GetEnableIRQ(uint32_t irq) { return irq == USART1_IRQn || irq == DMA2_Channel7_IRQn; }
void HAL_NVIC_DisableIRQ(uint32_t) {}
void HAL_NVIC_EnableIRQ(uint32_t irq) {
    if (irq == USART1_IRQn) ++uart_irq_enable_calls;
    if (irq == DMA2_Channel7_IRQn) ++dma_irq_enable_calls;
}
void NVIC_ClearPendingIRQ(uint32_t) { ++clear_pending_calls; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t) {
    if (sem->count > 0) {
        --sem->count;
        return pdTRUE;
    }
    return pdFALSE;
}
int rx_pending = 2;
int tx_pending = 2;
BaseType_t xQueueReceive(QueueHandle_t, RxQueueMsg *, TickType_t) {
    if (rx_pending > 0) {
        --rx_pending;
        ++rx_queue_drains;
        return pdPASS;
    }
    return pdFALSE;
}

struct CrabitWire {
    struct Message {
        char data[8] = {};
    };
};
class LinkQueue {
public:
    static bool receiveTx(CrabitWire::Message &, TickType_t) {
        if (tx_pending > 0) {
            --tx_pending;
            ++tx_queue_drains;
            return true;
        }
        return false;
    }
};

// PRODUCTION_INSERTION_POINT

static void reset_fixture() {
    owner_mode.store(static_cast<uint8_t>(UART_OWNER_NORMAL));
    pending_baud.store(0);
    rx_pending = 2;
    tx_pending = 2;
    usart.CR1 = USART_CR1_IDLEIE;
    huart1.Instance = &usart;
    huart1.hdmarx = &hdma;
    huart1.gState = HAL_UART_STATE_READY;
    huart1.RxState = HAL_UART_STATE_BUSY_RX;
    huart1.Init.BaudRate = 460800;
    tx_sem.count = 0;
    rx_write_idx = 1;
    hal_deinit_calls = hal_init_calls = hal_rx_calls = 0;
    abort_rx_calls = abort_tx_calls = clear_pending_calls = 0;
    uart_irq_enable_calls = dma_irq_enable_calls = 0;
    rx_queue_drains = tx_queue_drains = 0;
    fail_deinit = fail_init = fail_rx = false;
    fail_abort_rx = fail_abort_tx = false;
    owner_was_normal_during_rx_arm = false;
    programmed_baud = 0;
}

static void test_flash_claim_drains_and_quiesces() {
    reset_fixture();
    UartOwner_RequestLineCoding(921600);
    assert(UartOwner_Active() == UART_OWNER_NORMAL);
    assert(UartOwner_Claim(UART_OWNER_FLASH));
    assert(UartOwner_Active() == UART_OWNER_FLASH);
    assert((usart.CR1 & USART_CR1_IDLEIE) == 0);
    assert(abort_rx_calls == 1);
    assert(rx_write_idx == 0);
    assert(rx_queue_drains == 2);
    uint32_t pending = 0;
    assert(UartOwner_TakePendingLineCoding(&pending));
    assert(pending == 921600);
    assert(!UartOwner_TakePendingLineCoding(&pending));
    assert(!UartOwner_Claim(UART_OWNER_BYPASS));
    assert(UartOwner_Active() == UART_OWNER_FLASH);
}

static void test_release_restores_rx_before_reopening() {
    reset_fixture();
    assert(UartOwner_Claim(UART_OWNER_FLASH));
    assert(UartOwner_Release(UART_OWNER_FLASH));
    assert(UartOwner_Active() == UART_OWNER_NORMAL);
    assert(hal_deinit_calls == 1);
    assert(hal_init_calls == 1);
    assert(hal_rx_calls == 1);
    assert(programmed_baud == 460800);
    assert(!owner_was_normal_during_rx_arm);  // reopened only after RX arm
    assert(uart_irq_enable_calls >= 1);
    assert(dma_irq_enable_calls == 1);
    assert(tx_queue_drains == 2);
}

static void test_bypass_release_restores_after_deinit() {
    reset_fixture();
    assert(UartOwner_Claim(UART_OWNER_BYPASS));
    // BW16_Release_Bus() deinitializes the UART while the gate is held, so the
    // release path must restore it without poking unclocked registers first.
    assert(HAL_UART_DeInit(&huart1) == HAL_OK);
    assert(huart1.gState == HAL_UART_STATE_RESET);
    assert(huart1.RxState == HAL_UART_STATE_RESET);
    const unsigned abort_before = abort_rx_calls;
    usart.CR1 = USART_CR1_IDLEIE;
    assert(UartOwner_Release(UART_OWNER_BYPASS));
    assert(abort_rx_calls == abort_before);
    assert(UartOwner_Active() == UART_OWNER_NORMAL);
    assert(programmed_baud == 460800);
    assert(hal_deinit_calls == 1);  // pre-existing RESET is not deinitialized twice
    assert(hal_init_calls == 1);
    assert(hal_rx_calls == 1);
}

static void test_release_failure_stays_closed() {
    reset_fixture();
    assert(UartOwner_Claim(UART_OWNER_BYPASS));
    fail_init = true;
    assert(!UartOwner_Release(UART_OWNER_BYPASS));
    assert(UartOwner_Active() == UART_OWNER_FAULT);
    assert(!UartOwner_Normal());
    assert(hal_rx_calls == 0);
}

static void test_claim_abort_failure_is_safe() {
    reset_fixture();
    fail_abort_rx = true;
    assert(!UartOwner_Claim(UART_OWNER_FLASH));
    assert(UartOwner_Active() == UART_OWNER_FAULT);
}

static void test_line_coding_is_passive_in_normal_mode() {
    reset_fixture();
    assert(UartOwner_RequestLineCoding(115200));
    uint32_t pending = 0;
    assert(!UartOwner_TakePendingLineCoding(&pending));
    assert(huart1.Init.BaudRate == 460800);
    assert(UartOwner_LineCodingBaud() == 460800);
    assert(!UartOwner_RequestLineCoding(0));
}

int main() {
    test_flash_claim_drains_and_quiesces();
    test_release_restores_rx_before_reopening();
    test_bypass_release_restores_after_deinit();
    test_release_failure_stays_closed();
    test_claim_abort_failure_is_safe();
    test_line_coding_is_passive_in_normal_mode();
    std::puts("UART owner gate host checks passed");
    return 0;
}
