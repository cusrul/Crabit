#ifndef CRABIT_USB_COMPOSITE_H
#define CRABIT_USB_COMPOSITE_H

#include "usbd_core.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t CrabitUsb_Register(USBD_HandleTypeDef *pdev);
void *CrabitUsb_Allocate(uint32_t size);
void CrabitUsb_Free(void *pointer);

#ifdef __cplusplus
}
#endif

#endif
