#include "usb_composite.h"
#include "usb_composite_config.h"
#include "usbd_composite_builder.h"
#include "usbd_cdc.h"
#include "usbd_customhid.h"
#include "usbd_cdc_if.h"
#include "usb_keyboard.h"

#include <string.h>

typedef struct { uint8_t used; USBD_CDC_HandleTypeDef value; } CrabitCdcStorage;
typedef struct { uint8_t used; USBD_CUSTOM_HID_HandleTypeDef value; } CrabitHidStorage;
static CrabitCdcStorage cdcStorage;
static CrabitHidStorage hidStorage;

_Static_assert(sizeof(USBD_CDC_HandleTypeDef) != sizeof(USBD_CUSTOM_HID_HandleTypeDef), "class contexts must be distinguishable");

static uint8_t class_slot_matches(const USBD_HandleTypeDef *pdev, uint32_t slot,
                                  USBD_CompositeClassTypeDef type, uint32_t interfaceCount,
                                  uint32_t endpointCount, const uint8_t *interfaces,
                                  const uint8_t *endpoints)
{
  const USBD_CompositeElementTypeDef *entry = &pdev->tclasslist[slot];
  if (entry->Active != 1U || entry->ClassId != slot || entry->ClassType != type ||
      entry->NumIf != interfaceCount || entry->NumEps != endpointCount) return 0U;
  for (uint32_t i = 0U; i < interfaceCount; ++i)
    if (entry->Ifs[i] != interfaces[i]) return 0U;
  for (uint32_t i = 0U; i < endpointCount; ++i)
    if (entry->Eps[i].is_used != 1U || entry->Eps[i].add != endpoints[i]) return 0U;
  return 1U;
}

static uint8_t registered_slots_match(const USBD_HandleTypeDef *pdev)
{
  static const uint8_t cdcIfs[] = {0U, 1U};
  static const uint8_t hidIfs[] = {2U};
  static const uint8_t cdcEps[] = {CRABIT_USB_CDC_IN_EP, CRABIT_USB_CDC_OUT_EP, CRABIT_USB_CDC_CMD_EP};
  static const uint8_t hidEps[] = {CRABIT_USB_HID_IN_EP, CRABIT_USB_HID_OUT_EP};
  uint32_t activeCount = 0U;
  for (uint32_t i = 0U; i < USBD_MAX_SUPPORTED_CLASS; ++i)
    if (pdev->tclasslist[i].Active != 0U) ++activeCount;
  return (uint8_t)(pdev->NumClasses == CRABIT_USB_CLASS_COUNT && activeCount == CRABIT_USB_CLASS_COUNT &&
      class_slot_matches(pdev, CRABIT_USB_CDC_CLASS, CLASS_TYPE_CDC, 2U, 3U, cdcIfs, cdcEps) &&
      class_slot_matches(pdev, CRABIT_USB_HID_CLASS, CLASS_TYPE_CHID, 1U, 2U, hidIfs, hidEps));
}

void *CrabitUsb_Allocate(uint32_t size)
{
  if (size == sizeof(cdcStorage.value) && cdcStorage.used == 0U) {
    cdcStorage.used = 1U;
    memset(&cdcStorage.value, 0, sizeof(cdcStorage.value));
    return &cdcStorage.value;
  }
  if (size == sizeof(hidStorage.value) && hidStorage.used == 0U) {
    hidStorage.used = 1U;
    memset(&hidStorage.value, 0, sizeof(hidStorage.value));
    return &hidStorage.value;
  }
  return NULL;
}

void CrabitUsb_Free(void *pointer)
{
  if (pointer == &cdcStorage.value) cdcStorage.used = 0U;
  else if (pointer == &hidStorage.value) hidStorage.used = 0U;
}

uint8_t CrabitUsb_Register(USBD_HandleTypeDef *pdev)
{
  static uint8_t cdcEndpoints[] = {CRABIT_USB_CDC_IN_EP, CRABIT_USB_CDC_OUT_EP, CRABIT_USB_CDC_CMD_EP};
  static uint8_t hidEndpoints[] = {CRABIT_USB_HID_IN_EP, CRABIT_USB_HID_OUT_EP};
  if (pdev == NULL || pdev->classId != 0U || pdev->NumClasses != 0U ||
      pdev->classId >= USBD_MAX_SUPPORTED_CLASS || pdev->NumClasses >= USBD_MAX_SUPPORTED_CLASS ||
      pdev->tclasslist[0].Active != 0U) return USBD_FAIL;
  static const uint8_t cdcIfs[] = {0U, 1U};
  if (USBD_RegisterClassComposite(pdev, &USBD_CDC, CLASS_TYPE_CDC, cdcEndpoints) != USBD_OK ||
      pdev->NumClasses != 1U || pdev->classId != 1U ||
      !class_slot_matches(pdev, CRABIT_USB_CDC_CLASS, CLASS_TYPE_CDC, 2U, 3U, cdcIfs, cdcEndpoints)) goto failed;
  pdev->classId = CRABIT_USB_CDC_CLASS;
  if (USBD_CDC_RegisterInterface(pdev, &USBD_Interface_fops_FS) != USBD_OK) goto failed;

  CrabitUsb_KeyboardPrepareClass();
  if (pdev->classId >= USBD_MAX_SUPPORTED_CLASS || pdev->NumClasses >= USBD_MAX_SUPPORTED_CLASS ||
      pdev->tclasslist[1].Active != 0U) goto failed;
  pdev->classId = 1U;
  if (USBD_RegisterClassComposite(pdev, &CrabitUsb_KeyboardClass, CLASS_TYPE_CHID, hidEndpoints) != USBD_OK ||
      pdev->NumClasses != CRABIT_USB_CLASS_COUNT || pdev->classId != CRABIT_USB_CLASS_COUNT ||
      !registered_slots_match(pdev)) goto failed;
  pdev->classId = CRABIT_USB_HID_CLASS;
  if (USBD_CUSTOM_HID_RegisterInterface(pdev, &CrabitUsb_KeyboardInterface) != USBD_OK) goto failed;
  pdev->classId = CRABIT_USB_CDC_CLASS;
  return USBD_OK;

failed:
  pdev->classId = CRABIT_USB_CDC_CLASS;
  return USBD_FAIL;
}
