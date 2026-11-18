#ifndef AMEBA_FRAME_TEST_COPROC_FAKE_H
#define AMEBA_FRAME_TEST_COPROC_FAKE_H
#include <stdint.h>
extern uint32_t HAL_GetTick();
extern void vTaskDelay(uint32_t);
extern void recordHardware(const char*);
extern uint32_t gpio_a7, gpio_a6;
extern bool fake_uart_rx_ready;
extern uint32_t fake_uart_rx_calls;
extern uint32_t fake_uart_rx_boot_tick;
extern uint32_t fake_uart_rx_call_tick;
extern bool fake_pb7_uart_af;
extern bool fake_uart_receive_to_idle;
extern bool fake_finish_claim_result;
struct SystemConfig { float brightness = 0.5f; bool invert_disp = false; };
extern SystemConfig sys_cfg;
extern volatile uint8_t crabit_pat_hold;
bool STM32_PrepareAmebaUartRx(void);
static const uint32_t GPIO_PIN_RESET=0, GPIO_PIN_SET=1;
static void* GPIOA=(void*)1;
static const uint32_t GPIO_PIN_7=7, GPIO_PIN_6=6;
inline uint32_t HAL_GPIO_ReadPin(void* port, uint32_t pin) { (void)port; return pin==GPIO_PIN_7 ? gpio_a7 : gpio_a6; }
struct FakeSPI { int unused; };
class Uart {
public:
    static bool prepareAmebaRx() { return STM32_PrepareAmebaUartRx(); }
    static bool configure() { return true; }
};
class Display {
public:
    static bool releaseBus() { recordHardware("Safe_SPI_Release"); return true; }
    static void beginClaim() {
        recordHardware("Safe_SPI_Claim");
        recordHardware("HAL_SPI_DeInit");
    }
    static bool finishClaim() {
        recordHardware("HAL_SPI_Init");
        recordHardware("OLED_Init");
        recordHardware("OLED_SetSleep:false");
        return fake_finish_claim_result;
    }
    static void setSleep(bool) {}
    static void setBrightness(float) {}
    static void setInvert(bool) {}
};
extern FakeSPI hspi3;
extern volatile bool g_coproc_active;
inline void BW16_NormalBoot() {
    recordHardware("BW16_NormalBoot");
    recordHardware("fake:PB7_INPUT_PULLUP");
    recordHardware("fake:UART_RX_STANDARD");
    fake_pb7_uart_af = false;
    fake_uart_receive_to_idle = false;
    fake_uart_rx_boot_tick = HAL_GetTick();
    vTaskDelay(170);
}
inline void BW16_ForceOff() { recordHardware("BW16_ForceOff"); vTaskDelay(50); }
inline void Safe_SPI_Release() { recordHardware("Safe_SPI_Release"); }
inline void Safe_SPI_Claim() { recordHardware("Safe_SPI_Claim"); }
inline int HAL_SPI_DeInit(FakeSPI*) { recordHardware("HAL_SPI_DeInit"); return 0; }
inline int HAL_SPI_Init(FakeSPI*) { recordHardware("HAL_SPI_Init"); return 0; }
inline void OLED_Init() { recordHardware("OLED_Init"); }
inline void OLED_SetSleep(bool asleep) { recordHardware(asleep ? "OLED_SetSleep:true" : "OLED_SetSleep:false"); }
#endif
