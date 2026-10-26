#ifndef CRABIT_USB_KEYBOARD_H
#define CRABIT_USB_KEYBOARD_H

#include "usbd_customhid.h"
#include "usb_script_program.h"

#ifdef __cplusplus
extern "C" {
#endif

extern USBD_ClassTypeDef CrabitUsb_KeyboardClass;
extern USBD_CUSTOM_HID_ItfTypeDef CrabitUsb_KeyboardInterface;
void CrabitUsb_KeyboardPrepareClass(void);
/* USB IRQ only: stop on suspend/disconnect, release on the next writable SOF. */
void CrabitUsb_KeyboardSuspend(void);

typedef enum {
  CRABIT_KEYBOARD_IDLE, CRABIT_KEYBOARD_RUNNING,
  CRABIT_KEYBOARD_DONE, CRABIT_KEYBOARD_FAILED
} CrabitUsb_KeyboardState;

/* Requests are task-side; SOF remains the only report producer. */
uint8_t CrabitUsb_KeyboardRequest(void);
CrabitUsb_KeyboardState CrabitUsb_KeyboardStatus(void);
/* The caller retains an immutable action array until Busy returns false. */
uint8_t CrabitUsb_KeyboardRun(const CrabitUsbKeyAction *actions, uint16_t count,
                             uint32_t budget_ms);
void CrabitUsb_KeyboardCancel(void);
uint8_t CrabitUsb_KeyboardBusy(void);
uint8_t CrabitUsb_KeyboardAvailable(void);

#ifdef __cplusplus
}
#endif
#endif
