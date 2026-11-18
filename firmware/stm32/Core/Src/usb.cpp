#include "usb.h"

#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "main.h"
#include "semphr.h"
#include "usb_device.h"
#include "usb_diag.h"
#include "usb_keyboard.h"
#include "usb_script.h"
#include "uart_owner.h"
#include "usbd_cdc_if.h"
#include "usbd_core.h"

extern USBD_HandleTypeDef hUsbDeviceFS;
extern "C" void CDC_Resume_Receive(void);
extern volatile uint8_t usb_rx_ready;
extern volatile uint32_t usb_rx_len;
extern uint8_t UserRxBufferFS[];

namespace {
SemaphoreHandle_t log_mutex = nullptr;
}

bool Usb::init() {
    MX_USB_DEVICE_Init();
    return true;
}

bool Usb::initLog() {
    log_mutex = xSemaphoreCreateMutex();
    return log_mutex != nullptr;
}

bool Usb::suspend() {
    bool ok = USBD_Stop(&hUsbDeviceFS) == USBD_OK;
    ok = (USBD_DeInit(&hUsbDeviceFS) == USBD_OK) && ok;
    HAL_PWREx_DisableVddUSB();
    return ok;
}

bool Usb::resume() {
    MX_USB_DEVICE_Init();
    return true;
}

bool Usb::active() {
    return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED ||
           hUsbDeviceFS.dev_state == USBD_STATE_ADDRESSED ||
           CrabitUsb_KeyboardBusy();
}

bool Usb::configured() {
    return hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED;
}

uint8_t Usb::state() {
    return hUsbDeviceFS.dev_state;
}

void Usb::log(const char* format, ...) {
    static char buffer[256];
    if (format == nullptr || __get_IPSR() != 0)
        return;
    // The flash bridge owns the CDC IN endpoint for a raw binary stream.
    if (UartOwner_Active() == UART_OWNER_FLASH)
        return;
    bool locked = false;
    if (log_mutex != nullptr) {
        locked = xSemaphoreTake(log_mutex, pdMS_TO_TICKS(100)) == pdTRUE;
        if (!locked)
            return;
    }
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length >= static_cast<int>(sizeof(buffer)))
        length = static_cast<int>(sizeof(buffer)) - 1;
    if (length > 0) {
        uint8_t retries = 5;
        while (CDC_Transmit_FS(reinterpret_cast<uint8_t*>(buffer), static_cast<uint16_t>(length)) ==
                   USBD_BUSY &&
               retries--) {
            vTaskDelay(1);
        }
    }
    if (locked)
        xSemaphoreGive(log_mutex);
}

void Usb::diagTick() {
    CrabitUsb_DiagTick();
}

void Usb::scriptHandle(const char* command, size_t length, char* reply, size_t capacity) {
    CrabitUsb_ScriptHandle(command, length, reply, capacity);
}

bool Usb::transmit(const uint8_t* data, uint16_t length) {
    // The flash bridge owns the CDC IN stream; diagnostics/trace records must
    // not be injected between its binary chunks.
    if (UartOwner_Active() == UART_OWNER_FLASH) return false;
    return CDC_Transmit_FS(const_cast<uint8_t*>(data), length) == USBD_OK;
}

bool Usb::transmitBridge(const uint8_t* data, uint16_t length) {
    return CDC_TransmitBridge_FS(data, length) == USBD_OK;
}

bool Usb::rxReady() {
    return usb_rx_ready != 0;
}

const uint8_t* Usb::rxData() {
    return UserRxBufferFS;
}

uint32_t Usb::rxLength() {
    return usb_rx_len;
}

void Usb::clearRx() {
    usb_rx_ready = 0;
}

void Usb::consumeRx() {
    usb_rx_ready = 0;
    CDC_Resume_Receive();
}

void Usb::resumeReceive() {
    CDC_Resume_Receive();
}

extern "C" void Usb_Init(void) {
    (void)Usb::init();
}
