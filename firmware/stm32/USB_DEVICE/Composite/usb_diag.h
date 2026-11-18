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
uint16_t CrabitWakeHistory_Read(char *out, uint16_t capacity, uint32_t index);
#ifdef __cplusplus
}
#endif
#endif
