#pragma once

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#include <atomic>

// The only owner of huart1, its DMA ring and the TX completion semaphore.
class Uart {
public:
    static bool init();
    static bool startRx();
    static bool send(const uint8_t* data, size_t length);
    static bool suspend();
    static bool resume();
    static bool configure();
    static bool ready();
    static bool prepareAmebaRx();

    static uint32_t rxEvents();
    static uint32_t rxErrors();
    static uint32_t frames();
    static uint32_t badFrames();
    static uint32_t uiDrops();
};

extern "C" void STM32_RestartUartRx(void);
void Modem_StartRx(void);
void Task_Modem(void* pvParameters);
void Task_TX_Worker(void* pvParameters);
