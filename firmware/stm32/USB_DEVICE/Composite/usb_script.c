#include "usb_script.h"

#include "usb_keyboard.h"
#include "usb_script_program.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>
#include <string.h>

#define SCRIPT_UPLOAD_TIMEOUT_MS 30000U
#define SCRIPT_DRIVER_GRACE_MS 2000U
#define SCRIPT_REPLY_MAX 192U
#define SCRIPT_COMMAND_MAX 160U

typedef enum {
  SCRIPT_EMPTY,
  SCRIPT_UPLOADING,
  SCRIPT_READY,
  SCRIPT_RUNNING,
  SCRIPT_DONE,
  SCRIPT_FAILED,
  SCRIPT_STOPPING,
  SCRIPT_STOPPED
} ScriptState;

typedef union {
  uint8_t bytes[CRABIT_USB_SCRIPT_MAX_BYTES];
  CrabitUsbKeyAction actions[CRABIT_USB_SCRIPT_MAX_ACTIONS];
} ScriptStorage;

_Static_assert(sizeof(((ScriptStorage *)0)->actions) <=
                   CRABIT_USB_SCRIPT_MAX_BYTES,
               "script actions must fit the bounded RAM buffer");

static ScriptStorage script_storage;
static ScriptState script_state;
static uint32_t script_id;
static uint32_t last_run_id;
static uint32_t upload_started_at;
static uint16_t script_length;
static uint16_t script_received;
static uint16_t script_action_count;
static uint32_t script_runtime_ms;

static uint8_t span_equals(const char *text, size_t length, const char *literal)
{
  size_t literal_length = strlen(literal);
  return (uint8_t)(length == literal_length &&
                   memcmp(text, literal, length) == 0);
}

static uint8_t parse_hex_nibble(char ch, uint8_t lowercase_only,
                                uint8_t *value)
{
  if (ch >= '0' && ch <= '9') *value = (uint8_t)(ch - '0');
  else if (ch >= 'a' && ch <= 'f') *value = (uint8_t)(ch - 'a' + 10);
  else if (!lowercase_only && ch >= 'A' && ch <= 'F') {
    *value = (uint8_t)(ch - 'A' + 10);
  } else return 0U;
  return 1U;
}

static uint8_t parse_id(const char *text, size_t length, uint32_t *value)
{
  uint32_t parsed = 0U;
  if (length != 8U) return 0U;
  for (size_t i = 0U; i < length; ++i) {
    uint8_t nibble;
    if (!parse_hex_nibble(text[i], 1U, &nibble)) return 0U;
    parsed = (parsed << 4) | nibble;
  }
  if (parsed == 0U) return 0U;
  *value = parsed;
  return 1U;
}

static uint8_t parse_decimal(const char *text, size_t length, uint32_t maximum,
                             uint32_t *value)
{
  uint32_t parsed = 0U;
  if (length == 0U) return 0U;
  for (size_t i = 0U; i < length; ++i) {
    uint32_t digit;
    if (text[i] < '0' || text[i] > '9') return 0U;
    digit = (uint32_t)(text[i] - '0');
    if (parsed > (maximum - digit) / 10U) return 0U;
    parsed = parsed * 10U + digit;
  }
  *value = parsed;
  return 1U;
}

static const char *state_name(void)
{
  switch (script_state) {
    case SCRIPT_UPLOADING: return "uploading";
    case SCRIPT_READY: return "ready";
    case SCRIPT_RUNNING: return "running";
    case SCRIPT_DONE: return "done";
    case SCRIPT_FAILED: return "failed";
    case SCRIPT_STOPPING: return "stopping";
    case SCRIPT_STOPPED: return "stopped";
    case SCRIPT_EMPTY:
    default: return "empty";
  }
}

static void clear_program(void)
{
  memset(&script_storage, 0, sizeof(script_storage));
  script_id = 0U;
  script_length = 0U;
  script_received = 0U;
  script_action_count = 0U;
  script_runtime_ms = 0U;
  upload_started_at = 0U;
  script_state = SCRIPT_EMPTY;
}

static uint8_t sync_backend(uint32_t now)
{
  uint8_t busy = CrabitUsb_KeyboardBusy();
  CrabitUsb_KeyboardState backend_state = CrabitUsb_KeyboardStatus();
  uint8_t timed_out = 0U;

  if (script_state == SCRIPT_UPLOADING &&
      (uint32_t)(now - upload_started_at) >= SCRIPT_UPLOAD_TIMEOUT_MS) {
    clear_program();
    timed_out = 1U;
  }
  if (script_state == SCRIPT_STOPPING) {
    if (!busy) script_state = SCRIPT_STOPPED;
    return timed_out;
  }
  if (script_state != SCRIPT_RUNNING) return timed_out;
  if (backend_state == CRABIT_KEYBOARD_DONE) {
    script_state = SCRIPT_DONE;
  } else if (backend_state == CRABIT_KEYBOARD_FAILED) {
    script_state = SCRIPT_FAILED;
  } else if (backend_state == CRABIT_KEYBOARD_IDLE && !busy) {
    script_state = SCRIPT_FAILED;
  }
  return timed_out;
}

static uint8_t mutation_busy(void)
{
  return CrabitUsb_KeyboardBusy();
}

static const char *find_colon(const char *text, const char *end)
{
  while (text < end && *text != ':') ++text;
  return text < end ? text : NULL;
}

static uint8_t parse_command_id(const char *command, size_t length,
                                const char *prefix, size_t prefix_length,
                                uint32_t *id)
{
  if (length < prefix_length ||
      memcmp(command, prefix, prefix_length) != 0) return 0U;
  return parse_id(command + prefix_length, length - prefix_length, id);
}

static void reply_with(char *reply, size_t capacity, const char *result,
                       const char *error)
{
  size_t bounded_capacity = capacity < SCRIPT_REPLY_MAX ? capacity : SCRIPT_REPLY_MAX;
  if (bounded_capacity == 0U) return;
  (void)snprintf(reply, bounded_capacity,
                 "result=%s;state=%s;id=%08lx;received=%u;length=%u;error=%s;usb=%u",
                 result, state_name(), (unsigned long)script_id,
                 (unsigned int)script_received, (unsigned int)script_length,
                 error, (unsigned int)(CrabitUsb_KeyboardAvailable() != 0U));
  reply[bounded_capacity - 1U] = '\0';
}

void CrabitUsb_ScriptHandle(const char *command, size_t length,
                            char *reply, size_t capacity)
{
  const char *result = "invalid";
  const char *error = "syntax";
  uint32_t now = HAL_GetTick();
  uint8_t timed_out;

  if (reply == NULL || capacity == 0U) return;
  reply[0] = '\0';
  timed_out = sync_backend(now);

  if (command == NULL || length > SCRIPT_COMMAND_MAX) {
    reply_with(reply, capacity, "invalid", "syntax");
    return;
  }

  if (span_equals(command, length, "status")) {
    reply_with(reply, capacity, "snapshot", timed_out ? "timeout" : "none");
    return;
  }

  if (length >= 6U && memcmp(command, "begin:", 6U) == 0) {
    const char *end = command + length;
    const char *separator = find_colon(command + 6U, end);
    uint32_t id, program_length;
    if (separator == NULL || !parse_id(command + 6U,
                                       (size_t)(separator - (command + 6U)), &id)) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (!parse_decimal(separator + 1U, (size_t)(end - separator - 1U),
                       CRABIT_USB_SCRIPT_MAX_BYTES, &program_length) ||
        program_length == 0U) {
      reply_with(reply, capacity, "invalid", "length");
      return;
    }
    if (mutation_busy()) {
      reply_with(reply, capacity, "busy", "busy");
      return;
    }
    if (script_state == SCRIPT_UPLOADING || script_state == SCRIPT_RUNNING ||
        script_state == SCRIPT_STOPPING || id == last_run_id || id == script_id) {
      reply_with(reply, capacity, "invalid", id == last_run_id ? "replay" : "state");
      return;
    }
    clear_program();
    script_id = id;
    script_length = (uint16_t)program_length;
    script_received = 0U;
    upload_started_at = now;
    script_state = SCRIPT_UPLOADING;
    reply_with(reply, capacity, "accepted", "none");
    return;
  }

  if (length >= 6U && memcmp(command, "chunk:", 6U) == 0) {
    const char *end = command + length;
    const char *offset_separator = find_colon(command + 6U, end);
    const char *hex_separator;
    uint32_t id, offset;
    uint8_t decoded[64];
    size_t byte_count;
    if (offset_separator == NULL ||
        !parse_id(command + 6U,
                  (size_t)(offset_separator - (command + 6U)), &id)) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    hex_separator = find_colon(offset_separator + 1U, end);
    if (hex_separator == NULL ||
        !parse_decimal(offset_separator + 1U,
                       (size_t)(hex_separator - offset_separator - 1U),
                       CRABIT_USB_SCRIPT_MAX_BYTES, &offset)) {
      reply_with(reply, capacity, "invalid", "offset");
      return;
    }
    size_t hex_length = (size_t)(end - hex_separator - 1U);
    if (hex_length == 0U || (hex_length & 1U) != 0U || hex_length > 128U) {
      reply_with(reply, capacity, "invalid", "hex");
      return;
    }
    byte_count = hex_length / 2U;
    for (size_t i = 0U; i < byte_count; ++i) {
      uint8_t high, low;
      if (!parse_hex_nibble(hex_separator[1U + i * 2U], 0U, &high) ||
          !parse_hex_nibble(hex_separator[2U + i * 2U], 0U, &low)) {
        reply_with(reply, capacity, "invalid", "hex");
        return;
      }
      decoded[i] = (uint8_t)((high << 4) | low);
    }
    if (mutation_busy()) {
      reply_with(reply, capacity, "busy", "busy");
      return;
    }
    if (script_state != SCRIPT_UPLOADING) {
      reply_with(reply, capacity, "invalid", "state");
      return;
    }
    if (id != script_id) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (offset != script_received) {
      reply_with(reply, capacity, "invalid", "offset");
      return;
    }
    if (byte_count > (size_t)(script_length - script_received)) {
      reply_with(reply, capacity, "invalid", "length");
      return;
    }
    memcpy(script_storage.bytes + script_received, decoded, byte_count);
    script_received = (uint16_t)(script_received + byte_count);
    reply_with(reply, capacity, "accepted", "none");
    return;
  }

  if (length >= 7U && memcmp(command, "commit:", 7U) == 0) {
    uint32_t id;
    CrabitUsbKeyAction candidate[CRABIT_USB_SCRIPT_MAX_ACTIONS];
    uint16_t action_count = 0U;
    uint32_t runtime_ms = 0U;
    const char *parse_error = "none";
    if (!parse_command_id(command, length, "commit:", 7U, &id)) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (mutation_busy()) {
      reply_with(reply, capacity, "busy", "busy");
      return;
    }
    if (script_state != SCRIPT_UPLOADING) {
      reply_with(reply, capacity, "invalid", "state");
      return;
    }
    if (id != script_id) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (script_received != script_length) {
      reply_with(reply, capacity, "invalid", "incomplete");
      return;
    }
    if (!CrabitUsb_ScriptProgramParse((const char *)script_storage.bytes,
                                       script_length, candidate,
                                       CRABIT_USB_SCRIPT_MAX_ACTIONS,
                                       &action_count, &runtime_ms,
                                       &parse_error)) {
      script_state = SCRIPT_FAILED;
      reply_with(reply, capacity, "failed", parse_error);
      return;
    }
    memcpy(script_storage.actions, candidate,
           (size_t)action_count * sizeof(candidate[0]));
    script_action_count = action_count;
    script_runtime_ms = runtime_ms;
    script_state = SCRIPT_READY;
    reply_with(reply, capacity, "ready", "none");
    return;
  }

  if (length >= 4U && memcmp(command, "run:", 4U) == 0) {
    uint32_t id;
    uint32_t driver_budget;
    uint8_t run_result;
    if (!parse_command_id(command, length, "run:", 4U, &id)) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (id != script_id) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (script_state != SCRIPT_READY) {
      reply_with(reply, capacity, "invalid",
                 id == last_run_id ? "replay" : "state");
      return;
    }
    if (mutation_busy()) {
      reply_with(reply, capacity, "busy", "busy");
      return;
    }
    if (!CrabitUsb_KeyboardAvailable()) {
      reply_with(reply, capacity, "unavailable", "usb");
      return;
    }
    /* The script's declared runtime is <=30 s. Two seconds allow SOF/task
     * quantization while keeping the driver's hard watchdog within 32 s. */
    driver_budget = script_runtime_ms + SCRIPT_DRIVER_GRACE_MS;
    run_result = CrabitUsb_KeyboardRun(script_storage.actions,
                                       script_action_count, driver_budget);
    if (run_result == USBD_OK) {
      last_run_id = script_id;
      script_state = SCRIPT_RUNNING;
      reply_with(reply, capacity, "queued", "none");
    } else if (run_result == USBD_BUSY) {
      reply_with(reply, capacity, "busy", "busy");
    } else {
      script_state = SCRIPT_FAILED;
      reply_with(reply, capacity, "failed", "driver");
    }
    return;
  }

  if (length >= 5U && memcmp(command, "stop:", 5U) == 0) {
    uint32_t id;
    if (!parse_command_id(command, length, "stop:", 5U, &id)) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (id != script_id) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (script_state == SCRIPT_STOPPED) {
      reply_with(reply, capacity, "stopped", "none");
      return;
    }
    if (script_state == SCRIPT_STOPPING) {
      reply_with(reply, capacity, "accepted", "none");
      return;
    }
    if (script_state != SCRIPT_RUNNING) {
      reply_with(reply, capacity, "invalid", "state");
      return;
    }
    CrabitUsb_KeyboardCancel();
    script_state = SCRIPT_STOPPING;
    reply_with(reply, capacity, "accepted", "none");
    return;
  }

  if (length >= 6U && memcmp(command, "clear:", 6U) == 0) {
    uint32_t id;
    if (!parse_command_id(command, length, "clear:", 6U, &id)) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (id != script_id) {
      reply_with(reply, capacity, "invalid", "id");
      return;
    }
    if (mutation_busy()) {
      reply_with(reply, capacity, "busy", "busy");
      return;
    }
    clear_program();
    reply_with(reply, capacity, "cleared", "none");
    return;
  }

  reply_with(reply, capacity, result, error);
}
