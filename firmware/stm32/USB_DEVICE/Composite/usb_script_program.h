#ifndef CRABIT_USB_SCRIPT_PROGRAM_H
#define CRABIT_USB_SCRIPT_PROGRAM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRABIT_USB_SCRIPT_MAX_ACTIONS 128U
#define CRABIT_USB_SCRIPT_MAX_BYTES 512U
#define CRABIT_USB_SCRIPT_MAX_LINE_BYTES 128U
#define CRABIT_USB_SCRIPT_MAX_RUNTIME_MS 30000U
#define CRABIT_USB_SCRIPT_KEY_RUNTIME_MS 40U

typedef struct {
  uint16_t delay_ms;
  uint8_t modifier;
  uint8_t key;
} CrabitUsbKeyAction;

/*
 * Parse a bounded, ASCII USB keyboard script into HID key usages. The caller
 * should provide a scratch action array and publish it only when this returns
 * nonzero; on failure, *action_count and *runtime_ms are zeroed.
 */
uint8_t CrabitUsb_ScriptProgramParse(const char *script, size_t length,
                                     CrabitUsbKeyAction *actions,
                                     uint16_t action_capacity,
                                     uint16_t *action_count,
                                     uint32_t *runtime_ms,
                                     const char **error);

#ifdef __cplusplus
}
#endif

#endif
