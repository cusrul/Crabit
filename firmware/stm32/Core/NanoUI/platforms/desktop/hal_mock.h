#ifndef HAL_MOCK_H
#define HAL_MOCK_H

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <SDL2/SDL.h>

// =========================================================
// Mock for GlobalStore & EventBus
// =========================================================
struct GlobalDataStore {
    bool wifi_connected = false;
    int  ble_client_count = 0;
    char current_word[32] = {0};
    char latest_weather[32] = {0};
    char latest_notify[64] = {0};
    char ble_status[16] = {0};
    char wifi_status[16] = {0};
    char net_time[16] = {0};
    int  rssi = 0;
    uint8_t sys_volume = 100;
    uint8_t active_watch_face = 0;
    uint16_t req_buzzer_freq = 0;
    uint16_t req_buzzer_duration = 0;
};
extern GlobalDataStore GlobalStore;

class SysAudio {
public:
    static void play(uint16_t f, uint16_t d) { printf("[AUDIO] %d Hz, %d ms\n", f, d); }
};

class EventBus {
public:
    static void publish(const char* t, const char* p) { printf("[EVENT] %s -> %s\n", t, p); }
};

class ConfigManager {
public:
    static void save() { printf("[SIM] Config Saved\n"); }
};

// =========================================================
// STM32 HAL & FreeRTOS Mocks
// =========================================================
#define GPIOA 0
#define GPIOB 1
#define GPIO_PIN_0 0
#define GPIO_PIN_1 1
#define GPIO_PIN_2 2
#define GPIO_PIN_5 5
#define GPIO_PIN_6 6
#define GPIO_PIN_7 7
#define GPIO_PIN_15 15
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1

typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
#define GPIO_MODE_OUTPUT_PP 0
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP 2
#define GPIO_NOPULL 3
#define GPIO_MODE_ANALOG 4
#define GPIO_AF6_SPI3 0

typedef struct { uint8_t Hours, Minutes, Seconds, WeekDay, Month, Date, Year; } RTC_TimeTypeDef, RTC_DateTypeDef;
#define RTC_FORMAT_BIN 0

struct SPI_HandleTypeDef { int i; };
extern SPI_HandleTypeDef hspi3;
struct UART_HandleTypeDef { int i; };
extern UART_HandleTypeDef huart1;
struct TIM_HandleTypeDef { int i; };
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim15;
#define TIM_CHANNEL_2 2

#define UART_CLEAR_OREF 0
#define HAL_OK 0
inline int hrtc = 0;
#define configTOTAL_HEAP_SIZE (128*1024)

inline void HAL_GPIO_WritePin(int p, int n, int s) {}
inline int HAL_GPIO_ReadPin(int p, int n) { return GPIO_PIN_SET; }
inline void HAL_GPIO_Init(int p, GPIO_InitTypeDef* i) {}
inline void HAL_Delay(uint32_t ms) { SDL_Delay(ms); }
inline uint32_t HAL_GetTick() { return SDL_GetTicks(); }
// hal_get_ticks 已经在 hal_time.h 中定义，不需要在此 mock
#define pdMS_TO_TICKS(x) (x)
inline void vTaskDelay(uint32_t t) { SDL_Delay(t); }
inline size_t xPortGetFreeHeapSize() { return 64*1024; }
inline size_t xPortGetMinimumEverFreeHeapSize() { return 32*1024; }

inline int HAL_RTC_GetTime(int* h, RTC_TimeTypeDef* t, int f) {
    (void)h;
    time_t raw; time(&raw); struct tm* info = localtime(&raw);
    if (info) { t->Hours = (uint8_t)info->tm_hour; t->Minutes = (uint8_t)info->tm_min; t->Seconds = (uint8_t)info->tm_sec; }
    return 0; // HAL_OK
}
inline int HAL_RTC_GetDate(int* h, RTC_DateTypeDef* d, int f) {
    (void)h;
    time_t raw; time(&raw); struct tm* info = localtime(&raw);
    if (info) { d->Year = (uint8_t)(info->tm_year % 100); d->Month = (uint8_t)(info->tm_mon + 1); d->Date = (uint8_t)info->tm_mday; d->WeekDay = (uint8_t)(info->tm_wday == 0 ? 7 : info->tm_wday); }
    return 0;
}

inline void HAL_SPI_DeInit(SPI_HandleTypeDef* h) {}
inline void HAL_SPI_Init(SPI_HandleTypeDef* h) {}
inline void HAL_UART_Transmit(UART_HandleTypeDef* h, uint8_t* d, uint16_t s, uint32_t t) {}
inline int HAL_UART_Receive(UART_HandleTypeDef* h, uint8_t* d, uint16_t s, uint32_t t) { return 1; } // HAL_ERROR
inline void HAL_TIM_PWM_Start(TIM_HandleTypeDef* h, uint32_t c) {}
inline void HAL_TIM_PWM_Stop(TIM_HandleTypeDef* h, uint32_t c) {}
inline void __HAL_TIM_SET_COMPARE(TIM_HandleTypeDef* h, uint32_t c, uint32_t v) {}
inline void __HAL_TIM_SET_AUTORELOAD(TIM_HandleTypeDef* h, uint32_t v) {}
inline void __HAL_UART_CLEAR_FLAG(UART_HandleTypeDef* h, uint32_t f) {}

inline void MX_USART1_UART_Init() {}
inline void OLED_Init() {}
inline void OLED_SetSleep(bool s) {}
inline void BW16_NormalBoot() {}
inline void Safe_SPI_Release() {}
inline void Safe_SPI_Claim() {}
inline void BW16_Release_Bus() {}
inline void BW16_Restore_Bus() {}
inline void OLED_WR_Byte(uint8_t d, uint8_t c) {}
inline void USB_LOG(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }

#endif
