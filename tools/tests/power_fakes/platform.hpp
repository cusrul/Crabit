#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "idle_sleep_policy.h"
#include "mcu_stop2_sequence.h"
#include "wake_metrics.h"

using HAL_StatusTypeDef = enum { HAL_OK = 0, HAL_ERROR = 1 };

struct I2C_HandleTypeDef {
    int unused;
};
struct UART_HandleTypeDef {
    int unused;
};
struct SPI_HandleTypeDef {
    int unused;
};
struct USBD_HandleTypeDef {
    int unused;
};
struct GPIO_TypeDef {
    int unused;
};

struct GPIO_InitTypeDef {
    uint32_t Pin = 0;
    uint32_t Mode = 0;
    uint32_t Pull = 0;
};

enum GPIO_PinState { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 };
enum IRQn_Type { EXTI0_IRQn, EXTI1_IRQn, EXTI9_5_IRQn };

constexpr uint32_t GPIO_PIN_0 = 1u << 0;
constexpr uint32_t GPIO_PIN_1 = 1u << 1;
constexpr uint32_t GPIO_PIN_3 = 1u << 3;
constexpr uint32_t GPIO_PIN_6 = 1u << 6;
constexpr uint32_t GPIO_PIN_7 = 1u << 7;
constexpr uint32_t GPIO_PIN_8 = 1u << 8;
constexpr uint32_t GPIO_PIN_14 = 1u << 14;
constexpr uint32_t GPIO_MODE_IT_FALLING = 1;
constexpr uint32_t GPIO_MODE_INPUT = 2;
constexpr uint32_t GPIO_PULLUP = 1;
constexpr uint32_t PWR_FLAG_WU = 1;
constexpr uint32_t PWR_STOPENTRY_WFI = 1;

extern GPIO_TypeDef gpioa;
extern GPIO_TypeDef gpioh;
#define GPIOA (&gpioa)
#define GPIOH (&gpioh)

struct SystemConfig {
    float brightness = 0.5f;
    bool invert_disp = false;
    bool enable_lift_wake = true;
    bool enable_tap_wake = true;
    float wake_x_max = 1.0f;
    float wake_y_min = 1.0f;
    float wake_y_max = 2.0f;
    float wake_z_min = 0.0f;
    uint32_t screen_off_ms = 1000;
    uint32_t deep_sleep_ms = 1000;
};

struct StmImuSnapshot {
    bool is_valid = false;
    uint32_t timestamp = 0;
    float ax = 0;
    float ay = 0;
    float az = 0;
};
struct StmSensorSnapshot {
    StmImuSnapshot imu;
};

struct PAT9125_Driver {
    void sleep(bool sleeping);
    void init();
};
struct QMI8658_Driver {
    void sleepForWakeOnMotion();
    void init();
};
struct BMP280_Driver {
    void init();
};

class WindowManager {
   public:
    static WindowManager* instance;
    unsigned int stack_size = 1;
    unsigned int getStackSize() const;
    void markDirty();
};

class AppManager {
   public:
    static void exitCurrentApp();
};

class BadUsbArmory {
   public:
    static bool hasPendingTasks();
};

extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;
extern SPI_HandleTypeDef hspi3;
extern USBD_HandleTypeDef hUsbDeviceFS;
extern SystemConfig sys_cfg;
extern volatile bool g_coproc_active;
extern volatile uint32_t g_qmi_int1_cnt;
extern volatile uint32_t g_qmi_tap_cnt;
extern volatile uint32_t g_qmi_tap_tick;
extern volatile uint32_t g_pat_int_cnt;
extern PAT9125_Driver pat_drv;
extern QMI8658_Driver imu_drv;
extern BMP280_Driver bmp_drv;
extern WindowManager wm;
extern uint8_t rx_pingpong[2][512];
extern volatile uint8_t rx_write_idx;

namespace screen_power_fake {
extern uint32_t now_ms;
extern bool pa6_down;
extern HAL_StatusTypeDef checked_voltage_result;
extern HAL_StatusTypeDef exit_power_result;
extern unsigned int checked_voltage_calls;
extern std::vector<uint16_t> checked_voltage_requests;
extern std::vector<std::string> events;
extern bool sensor_read_available;
extern StmSensorSnapshot sensor_snapshot;

void reset();
void record(const char* event);
unsigned int count(const char* event);
int indexOf(const char* event);
void clearSensor();
void publishImu(uint32_t timestamp, bool valid, float ax, float ay, float az);
}  // namespace screen_power_fake

namespace McuStop2 {
inline Reason blocked_reason = Reason::None;
inline Result next_result{Reason::Qmi, true, 1, 80000000};
class WatchPort {
   public:
    Reason blocked() { return blocked_reason; }
};
inline Result runWatch() {
    screen_power_fake::record("stop2");
    return next_result;
}
}  // namespace McuStop2
inline void STM32_ResetInputAfterWake() {
    screen_power_fake::record("input_reset");
}

uint32_t HAL_GetTick();
void HAL_NVIC_DisableIRQ(IRQn_Type irq);
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq);
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t priority, uint32_t subpriority);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);
void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint32_t pin);
void HAL_GPIO_ClearExti(uint32_t pin);
void HAL_SuspendTick();
void HAL_ResumeTick();
void HAL_PWREx_DisableVddUSB();
void HAL_PWREx_EnterSTOP2Mode(uint32_t entry);
void HAL_PWR_ClearFlag(uint32_t flag);
void HAL_UART_DeInit(UART_HandleTypeDef* handle);
void HAL_SPI_DeInit(SPI_HandleTypeDef* handle);
void HAL_I2C_DeInit(I2C_HandleTypeDef* handle);
uint8_t USBD_Stop(USBD_HandleTypeDef* device);
uint8_t USBD_DeInit(USBD_HandleTypeDef* device);
bool STM32_ReadSensorSnapshot(StmSensorSnapshot* snapshot);

HAL_StatusTypeDef AXP2101_SetDC1VoltageChecked(I2C_HandleTypeDef* handle, uint16_t mv);
HAL_StatusTypeDef AXP2101_EnterLowPower(I2C_HandleTypeDef* handle);
HAL_StatusTypeDef AXP2101_ExitLowPower(I2C_HandleTypeDef* handle);
void OLED_SetSleep(bool sleep);
void OLED_WR_Byte(uint8_t value, uint8_t command);

#define __HAL_GPIO_EXTI_CLEAR_IT(pin) HAL_GPIO_ClearExti(pin)
#define __HAL_PWR_CLEAR_FLAG(flag) HAL_PWR_ClearFlag(flag)
#define USB_LOG(...) \
    do {             \
    } while (false)
