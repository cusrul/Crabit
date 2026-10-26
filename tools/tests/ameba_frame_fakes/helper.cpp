#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#define RX_BUF_LEN 512

struct FakeUSART { uint32_t CR1; };
struct UART_HandleTypeDef { FakeUSART* Instance; void* hdmarx; };
struct GPIO_InitTypeDef { uint32_t Pin, Mode, Pull, Speed, Alternate; };
static FakeUSART fake_usart;
static UART_HandleTypeDef huart1{&fake_usart, nullptr};
static uint8_t rx_pingpong[2][512];
static volatile uint8_t rx_write_idx;
static const int USART1_IRQn = 1, DMA2_Channel7_IRQn = 2, DMA1_Channel4_IRQn = 3;
static const uint32_t GPIO_PIN_7 = 7, GPIO_MODE_AF_PP = 1, GPIO_NOPULL = 0;
static const uint32_t GPIO_SPEED_FREQ_VERY_HIGH = 3, GPIO_AF7_USART1 = 7;
static const uint32_t USART_CR1_IDLEIE = 1, USART_CR1_TCIE = 2, DMA_IT_HT = 2;
static const int HAL_OK = 0, HAL_ERROR = 1;
static void* GPIOB = reinterpret_cast<void*>(0xB);
static uint32_t enabled[4] = {}, pending[4] = {};
static bool abort_ok = true, receive_ok = true;
static uint32_t abort_calls, receive_calls, disable_it_calls, disable_ht_calls;
static uint32_t nvic_disable_calls, nvic_enable_calls;
static uint8_t* received_buffer;
static uint16_t received_size;
static GPIO_InitTypeDef last_gpio;
static void* last_gpio_port;
static uint32_t gpio_calls;
static std::vector<int> call_order;
static bool abort_saw_quiesced_state, gpio_after_abort, receive_after_gpio;
static bool ht_after_successful_receive;
static uint32_t blocked_tx_callbacks, invoked_tx_callbacks;

static uint32_t NVIC_GetEnableIRQ(int irq) { return enabled[irq]; }
static uint32_t NVIC_GetPendingIRQ(int irq) { return pending[irq]; }
static void HAL_NVIC_DisableIRQ(int irq) {
    ++nvic_disable_calls; enabled[irq] = 0; call_order.push_back(10 + irq);
}
static void HAL_NVIC_EnableIRQ(int irq) {
    ++nvic_enable_calls; enabled[irq] = 1; call_order.push_back(20 + irq);
}
static void atomic_clear_bit(volatile uint32_t* reg, uint32_t mask) {
    ++disable_it_calls; call_order.push_back(1); *reg &= ~mask;
}
static void simulate_tx_callbacks() {
    if (enabled[DMA1_Channel4_IRQn]) invoked_tx_callbacks += 2;
    else blocked_tx_callbacks += 2;
}
static void fake_gpio_init(void* port, GPIO_InitTypeDef* pin) {
    gpio_after_abort = abort_calls == 1;
    simulate_tx_callbacks();
    ++gpio_calls; last_gpio_port = port; last_gpio = *pin; call_order.push_back(3);
}
static int HAL_UART_AbortReceive(UART_HandleTypeDef*) {
    ++abort_calls;
    abort_saw_quiesced_state = enabled[USART1_IRQn] == 0 &&
        enabled[DMA2_Channel7_IRQn] == 0 && enabled[DMA1_Channel4_IRQn] == 0 &&
        disable_it_calls == 1 && !(fake_usart.CR1 & USART_CR1_IDLEIE) &&
        (fake_usart.CR1 & USART_CR1_TCIE);
    simulate_tx_callbacks(); call_order.push_back(2);
    return abort_ok ? HAL_OK : HAL_ERROR;
}
static int HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef*, uint8_t* buffer,
                                        uint16_t size) {
    receive_after_gpio = gpio_calls == 1;
    simulate_tx_callbacks();
    ++receive_calls; received_buffer = buffer; received_size = size; call_order.push_back(4);
    return receive_ok ? HAL_OK : HAL_ERROR;
}
static void fake_disable_dma_it(void*, uint32_t it) {
    ht_after_successful_receive = receive_calls == 1 && receive_ok;
    ++disable_ht_calls;
    if (it == DMA_IT_HT) call_order.push_back(5);
}
#define ATOMIC_CLEAR_BIT(reg, mask) atomic_clear_bit(&(reg), (mask))
#define HAL_GPIO_Init(p, i) fake_gpio_init((p), (i))
#define __HAL_DMA_DISABLE_IT(h, i) fake_disable_dma_it((h), (i))

// PRODUCTION_HELPER_INSERTION_POINT
static int failed;
#define CHECK(v) do { if (!(v)) { fprintf(stderr, "helper FAIL line %d: %s\n", __LINE__, #v); ++failed; } } while (0)
extern "C" bool STM32_PrepareAmebaUartRx(void);

static void reset(bool uart_irq, bool dma_irq, bool tx_irq, uint32_t idx) {
    enabled[USART1_IRQn] = uart_irq;
    enabled[DMA2_Channel7_IRQn] = dma_irq;
    enabled[DMA1_Channel4_IRQn] = tx_irq;
    pending[USART1_IRQn] = 1; pending[DMA2_Channel7_IRQn] = 0;
    pending[DMA1_Channel4_IRQn] = 1;
    fake_usart.CR1 = USART_CR1_IDLEIE | USART_CR1_TCIE;
    abort_ok = receive_ok = true;
    abort_calls = receive_calls = disable_it_calls = disable_ht_calls = 0;
    nvic_disable_calls = nvic_enable_calls = gpio_calls = 0;
    received_buffer = nullptr; received_size = 0;
    memset(&last_gpio, 0, sizeof(last_gpio)); last_gpio_port = nullptr;
    rx_write_idx = idx; huart1.hdmarx = reinterpret_cast<void*>(0xD00D);
    call_order.clear(); blocked_tx_callbacks = invoked_tx_callbacks = 0;
    abort_saw_quiesced_state = gpio_after_abort = receive_after_gpio = false;
    ht_after_successful_receive = false;
}
static void check_success(bool uart_irq, bool dma_irq, bool tx_irq, uint32_t idx) {
    reset(uart_irq, dma_irq, tx_irq, idx);
    CHECK(STM32_PrepareAmebaUartRx());
    CHECK(enabled[USART1_IRQn] == uint32_t(uart_irq));
    CHECK(enabled[DMA2_Channel7_IRQn] == uint32_t(dma_irq));
    CHECK(enabled[DMA1_Channel4_IRQn] == uint32_t(tx_irq));
    CHECK(nvic_disable_calls == 3);
    CHECK(nvic_enable_calls == uint32_t(uart_irq) + uint32_t(dma_irq) + uint32_t(tx_irq));
    CHECK(abort_calls == 1 && receive_calls == 1 && abort_saw_quiesced_state);
    CHECK(disable_it_calls == 1 && disable_ht_calls == 1);
    CHECK(gpio_after_abort && receive_after_gpio && ht_after_successful_receive);
    CHECK((fake_usart.CR1 & USART_CR1_IDLEIE) == 0);
    CHECK((fake_usart.CR1 & USART_CR1_TCIE) != 0); // atomic IDLE clear preserves TX TCIE
    CHECK(blocked_tx_callbacks == 6 && invoked_tx_callbacks == 0);
    CHECK(NVIC_GetPendingIRQ(USART1_IRQn) == 1 &&
          NVIC_GetPendingIRQ(DMA2_Channel7_IRQn) == 0 &&
          NVIC_GetPendingIRQ(DMA1_Channel4_IRQn) == 1);
    CHECK(gpio_calls == 1 && last_gpio_port == GPIOB);
    CHECK(last_gpio.Pin == GPIO_PIN_7 && last_gpio.Mode == GPIO_MODE_AF_PP);
    CHECK(last_gpio.Pull == GPIO_NOPULL && last_gpio.Speed == GPIO_SPEED_FREQ_VERY_HIGH);
    CHECK(last_gpio.Alternate == GPIO_AF7_USART1);
    CHECK(received_buffer == rx_pingpong[idx] && received_size == 511);
    CHECK(call_order.size() == 8 + uint32_t(uart_irq) + uint32_t(dma_irq) + uint32_t(tx_irq));
    CHECK(call_order[0] == 10 + USART1_IRQn && call_order[1] == 10 + DMA2_Channel7_IRQn &&
          call_order[2] == 10 + DMA1_Channel4_IRQn && call_order[3] == 1 &&
          call_order[4] == 2 && call_order[5] == 3 && call_order[6] == 4 &&
          call_order[7] == 5);
    uint32_t at = 8;
    if (tx_irq) CHECK(call_order[at++] == 20 + DMA1_Channel4_IRQn);
    if (dma_irq) CHECK(call_order[at++] == 20 + DMA2_Channel7_IRQn);
    if (uart_irq) CHECK(call_order[at++] == 20 + USART1_IRQn);
    CHECK(at == call_order.size());
}
int main() {
    for (uint32_t mask = 0; mask < 8; ++mask)
        check_success(mask & 1, mask & 2, mask & 4, (mask >> 1) & 1);
    reset(true, false, true, 0); abort_ok = false;
    CHECK(!STM32_PrepareAmebaUartRx());
    CHECK(abort_calls == 1 && receive_calls == 0 && gpio_calls == 0 && disable_ht_calls == 0);
    CHECK(abort_saw_quiesced_state && blocked_tx_callbacks == 2 && invoked_tx_callbacks == 0);
    CHECK(call_order.size() == 7 && call_order[5] == 20 + DMA1_Channel4_IRQn &&
          call_order[6] == 20 + USART1_IRQn);
    CHECK(enabled[USART1_IRQn] == 1 && enabled[DMA2_Channel7_IRQn] == 0 &&
          enabled[DMA1_Channel4_IRQn] == 1);
    reset(false, true, false, 1); receive_ok = false;
    CHECK(!STM32_PrepareAmebaUartRx());
    CHECK(abort_calls == 1 && receive_calls == 1 && gpio_calls == 1 && disable_ht_calls == 0);
    CHECK(abort_saw_quiesced_state && blocked_tx_callbacks == 6 && invoked_tx_callbacks == 0);
    CHECK(call_order.size() == 8 && call_order[7] == 20 + DMA2_Channel7_IRQn);
    CHECK(enabled[USART1_IRQn] == 0 && enabled[DMA2_Channel7_IRQn] == 1 &&
          enabled[DMA1_Channel4_IRQn] == 0);
    if (failed) return 1;
    puts("STM32 AMEBA UART RX helper mock checks passed");
    return 0;
}
