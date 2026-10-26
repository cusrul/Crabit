#include "usb_keyboard.h"
#include "usb_composite_config.h"
#include "usb_device.h"
#include "usb_diag.h"

extern USBD_HandleTypeDef hUsbDeviceFS;
extern volatile uint8_t bw16_flash_mode;

/* Same 8-byte keyboard / one-byte LEDs contract as the original HID app.
 * Report protocol only (not BIOS boot-keyboard compatibility). */
static uint8_t keyboard_descriptor[] = {
  0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x05,0x07,
  0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01,
  0x75,0x01, 0x95,0x08, 0x81,0x02, 0x95,0x01,
  0x75,0x08, 0x81,0x03, 0x95,0x05, 0x75,0x01,
  0x05,0x08, 0x19,0x01, 0x29,0x05, 0x91,0x02,
  0x95,0x01, 0x75,0x03, 0x91,0x03, 0x95,0x06,
  0x75,0x08, 0x15,0x00, 0x25,0x65, 0x05,0x07,
  0x19,0x00, 0x29,0x65, 0x81,0x00, 0xC0
};
_Static_assert(sizeof(keyboard_descriptor) == USBD_CUSTOM_HID_REPORT_DESC_SIZE,
               "keyboard report descriptor length");

/* Exactly "crabit usb test\n" in the host's US-layout key usages. */
static const uint8_t fixed_keys[] = {
  6,21,4,5,12,23,44,24,22,5,44,23,8,22,23,40
};
static uint8_t report[8]; /* Must outlive every asynchronous IN transfer. */
static uint8_t control_report[8], led_state;
static volatile CrabitUsb_KeyboardState state;
static uint8_t step;
static uint8_t release_pending;
static uint32_t started_at, last_report_at;
static uint32_t run_budget_ms;
static const CrabitUsbKeyAction *script_actions;
static uint16_t script_count, script_step;
static uint8_t script_release;

static int8_t keyboard_init(void)
{
  state = CRABIT_KEYBOARD_IDLE;
  step = release_pending = 0;
  script_actions = NULL;
  script_count = script_step = script_release = 0;
  run_budget_ms = 5000U;
  led_state = 0;
  memset(report, 0, sizeof(report));
  return USBD_OK;
}

static int8_t keyboard_deinit(void)
{
  /* Reset/unconfigure cancels; never replay an old request after reconnect. */
  if (state == CRABIT_KEYBOARD_RUNNING) state = CRABIT_KEYBOARD_FAILED;
  step = release_pending = 0;
  script_actions = NULL;
  return USBD_OK;
}

static int8_t keyboard_leds(uint8_t first, uint8_t second)
{
  (void)second;
  led_state = first & 0x1FU;
  /* SET_REPORT and interrupt OUT both reach this callback. Keep OUT armed. */
  return USBD_CUSTOM_HID_ReceivePacket(&hUsbDeviceFS);
}

static uint8_t *keyboard_get_report(uint16_t *length)
{
  /* Only report ID 0, Input and LED Output exist; no Feature reports. */
  switch (hUsbDeviceFS.request.wValue) {
    case 0x0100:
      memcpy(control_report, report, sizeof(control_report));
      *length = sizeof(control_report);
      return control_report;
    case 0x0200:
      *length = 1;
      return &led_state;
    default:
      *length = 0;
      return NULL;
  }
}

USBD_CUSTOM_HID_ItfTypeDef CrabitUsb_KeyboardInterface = {
  .pReport = keyboard_descriptor,
  .Init = keyboard_init,
  .DeInit = keyboard_deinit,
  .OutEvent = keyboard_leds,
  .GetReport = keyboard_get_report
};
USBD_ClassTypeDef CrabitUsb_KeyboardClass;

void CrabitUsb_KeyboardSuspend(void)
{
  if (state == CRABIT_KEYBOARD_RUNNING) {
    state = CRABIT_KEYBOARD_FAILED;
    release_pending = 1;
  }
}

static uint8_t keyboard_sof(USBD_HandleTypeDef *device)
{
  USBD_CUSTOM_HID_HandleTypeDef *hid =
      device->pClassDataCmsit[CRABIT_USB_HID_CLASS];
  uint32_t now = HAL_GetTick();
  /* The flash App owns the UI task until AFTER it clears flash mode. Observing
   * this boundary here prevents queued normal-mode TYPE replay on App exit.
   * USB suspend independently clears diagnostics in the target callback. */
  if (bw16_flash_mode) CrabitUsb_DiagReset();
  if (hid == NULL || device->dev_state != USBD_STATE_CONFIGURED) return USBD_OK;
  if (state == CRABIT_KEYBOARD_RUNNING &&
      (bw16_flash_mode || (uint32_t)(now - started_at) >= run_budget_ms)) {
    state = CRABIT_KEYBOARD_FAILED;
    release_pending = 1;
  }
  if (hid->state != CUSTOM_HID_IDLE) return USBD_OK;
  if (release_pending) {
    memset(report, 0, sizeof(report));
    if (USBD_CUSTOM_HID_SendReport(device, report, sizeof(report),
                                 CRABIT_USB_HID_CLASS) == USBD_OK) {
      release_pending = 0;
    }
    return USBD_OK;
  }
  if (state != CRABIT_KEYBOARD_RUNNING) return USBD_OK;
  if (script_actions != NULL) {
    if (script_step == script_count) {
      state = CRABIT_KEYBOARD_DONE;
      script_actions = NULL;
      return USBD_OK;
    }
    const CrabitUsbKeyAction *action = &script_actions[script_step];
    if (action->key == 0U) {
      if ((uint32_t)(now - last_report_at) >= action->delay_ms) {
        ++script_step;
        last_report_at = now;
      }
      return USBD_OK;
    }
    if ((uint32_t)(now - last_report_at) < 20U) return USBD_OK;
    memset(report, 0, sizeof(report));
    if (!script_release) {
      report[0] = action->modifier;
      report[2] = action->key;
    }
    if (USBD_CUSTOM_HID_SendReport(device, report, sizeof(report),
                                 CRABIT_USB_HID_CLASS) == USBD_OK) {
      if (script_release) ++script_step;
      script_release = !script_release;
      last_report_at = now;
    }
    return USBD_OK;
  }
  if (step == sizeof(fixed_keys) * 2U) {
    /* Final all-up transfer completed, not merely queued. */
    state = CRABIT_KEYBOARD_DONE;
    return USBD_OK;
  }
  if ((uint32_t)(now - last_report_at) < 20U) return USBD_OK;
  memset(report, 0, sizeof(report));
  if ((step & 1U) == 0U) report[2] = fixed_keys[step / 2U];
  if (USBD_CUSTOM_HID_SendReport(device, report, sizeof(report),
                               CRABIT_USB_HID_CLASS) == USBD_OK) {
    ++step;
    last_report_at = now;
  }
  return USBD_OK;
}

void CrabitUsb_KeyboardPrepareClass(void)
{
  /* Extend the class table, not the upstream driver implementation. SOF keeps
   * all-up cancellation alive even while the legacy flash App owns the UI task. */
  CrabitUsb_KeyboardClass = USBD_CUSTOM_HID;
  CrabitUsb_KeyboardClass.SOF = keyboard_sof;
}

uint8_t CrabitUsb_KeyboardRequest(void)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  uint8_t result = USBD_FAIL;
  USBD_CUSTOM_HID_HandleTypeDef *hid =
      hUsbDeviceFS.pClassDataCmsit[CRABIT_USB_HID_CLASS];
  if (!bw16_flash_mode && hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && hid) {
    if (state == CRABIT_KEYBOARD_RUNNING || release_pending || hid->state != CUSTOM_HID_IDLE) {
      result = USBD_BUSY;
    } else {
      step = 0;
      script_actions = NULL;
      run_budget_ms = 5000U;
      started_at = last_report_at = HAL_GetTick();
      state = CRABIT_KEYBOARD_RUNNING;
      result = USBD_OK;
    }
  }
  __set_PRIMASK(mask);
  return result;
}

CrabitUsb_KeyboardState CrabitUsb_KeyboardStatus(void)
{
  return state;
}

uint8_t CrabitUsb_KeyboardAvailable(void)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  uint8_t result = !bw16_flash_mode && hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED &&
      hUsbDeviceFS.pClassDataCmsit[CRABIT_USB_HID_CLASS] != NULL;
  __set_PRIMASK(mask);
  return result;
}

uint8_t CrabitUsb_KeyboardBusy(void)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  USBD_CUSTOM_HID_HandleTypeDef *hid = hUsbDeviceFS.pClassDataCmsit[CRABIT_USB_HID_CLASS];
  uint8_t result = state == CRABIT_KEYBOARD_RUNNING || release_pending ||
      (hid != NULL && hid->state != CUSTOM_HID_IDLE);
  __set_PRIMASK(mask);
  return result;
}

uint8_t CrabitUsb_KeyboardRun(const CrabitUsbKeyAction *actions, uint16_t count,
                             uint32_t budget_ms)
{
  if (actions == NULL || count == 0U || count > 128U ||
      budget_ms == 0U || budget_ms > 32000U) return USBD_FAIL;
  for (uint16_t i = 0; i < count; ++i) {
    if (actions[i].key > 0x65U || actions[i].delay_ms > 30000U) return USBD_FAIL;
  }
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  uint8_t result = USBD_FAIL;
  USBD_CUSTOM_HID_HandleTypeDef *hid = hUsbDeviceFS.pClassDataCmsit[CRABIT_USB_HID_CLASS];
  if (!bw16_flash_mode && hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && hid != NULL) {
    if (state == CRABIT_KEYBOARD_RUNNING || release_pending || hid->state != CUSTOM_HID_IDLE) {
      result = USBD_BUSY;
    } else {
      script_actions = actions;
      script_count = count;
      script_step = script_release = 0;
      run_budget_ms = budget_ms;
      started_at = last_report_at = HAL_GetTick();
      state = CRABIT_KEYBOARD_RUNNING;
      result = USBD_OK;
    }
  }
  __set_PRIMASK(mask);
  return result;
}

void CrabitUsb_KeyboardCancel(void)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (state == CRABIT_KEYBOARD_RUNNING) {
    state = CRABIT_KEYBOARD_FAILED;
    release_pending = 1;
  }
  __set_PRIMASK(mask);
}
