#ifndef USB_COMPOSITE_HOST_MAIN_H
#define USB_COMPOSITE_HOST_MAIN_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define __IO volatile
#define __PACKED __attribute__((packed))
#define __STATIC_INLINE static inline
#define UNUSED(x) ((void)(x))
typedef struct { struct { uint32_t maxpacket; } IN_ep[16]; } PCD_HandleTypeDef;
void HAL_Delay(uint32_t delay);
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
#endif
