#ifndef CRABIT_USB_SCRIPT_H
#define CRABIT_USB_SCRIPT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UI-task-only, RAM-only USB script upload and execution service. */
void CrabitUsb_ScriptHandle(const char *command, size_t length,
                            char *reply, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
