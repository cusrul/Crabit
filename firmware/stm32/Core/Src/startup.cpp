#include "FreeRTOS.h"
#include "task.h"

#include "display.h"
#include "main.h"
#include "nanoui_port.h"
#include "usb.h"

// Product startup. freertos.c only creates the default task and calls this
// function; the generated file no longer contains board or UI logic.
extern "C" void Crabit_Start(void) {
    Usb_Init();

    // Keep the existing BW16 power pulse and panel blanking before the UI
    // loop takes ownership.
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(500));
    Display::init();
    Display::fill(0x00);

    NanoUI_MainLoop();

    for (;;) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_3);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
