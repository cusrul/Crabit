#ifndef USB_COMPOSITE_HOST_CONF_H
#define USB_COMPOSITE_HOST_CONF_H
#include "main.h"
#include "usb_composite_config.h"
/* Real ST class/core headers and product endpoint/report configuration. */
#define USBD_MAX_NUM_INTERFACES CRABIT_USB_INTERFACE_COUNT
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SIZ 512U
#define USBD_LPM_ENABLED 1U
#define USBD_SELF_POWERED 1U
#define USBD_DEBUG_LEVEL 0U
#endif
