#ifndef CRABIT_USB_DIAG_H
#define CRABIT_USB_DIAG_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* RX/reset run in USB IRQ; Tick runs in the existing UI task. */
void CrabitUsb_DiagReceive(const uint8_t *bytes, uint32_t length);
void CrabitUsb_DiagReset(void);
void CrabitUsb_DiagTick(void);
#ifdef __cplusplus
}
#endif
#endif
