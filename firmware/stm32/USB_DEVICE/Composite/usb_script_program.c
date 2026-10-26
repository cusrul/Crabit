#include "usb_script_program.h"

#include <string.h>

static uint8_t is_hspace(char ch)
{
  return (uint8_t)(ch == ' ' || ch == '\t');
}

static uint8_t is_digit(char ch)
{
  return (uint8_t)(ch >= '0' && ch <= '9');
}

static uint8_t parse_decimal(const char *text, size_t length, uint32_t limit,
                             uint32_t *value)
{
  uint32_t parsed = 0U;
  if (length == 0U) return 0U;
  for (size_t i = 0U; i < length; ++i) {
    uint32_t digit;
    if (!is_digit(text[i])) return 0U;
    digit = (uint32_t)(text[i] - '0');
    if (parsed > (limit - digit) / 10U) return 0U;
    parsed = parsed * 10U + digit;
  }
  *value = parsed;
  return 1U;
}

static uint8_t append_action(CrabitUsbKeyAction *actions, uint16_t capacity,
                             uint16_t *count, uint32_t *runtime,
                             uint16_t delay_ms, uint8_t modifier,
                             uint8_t key, const char **error)
{
  uint32_t cost = key == 0U ? delay_ms : CRABIT_USB_SCRIPT_KEY_RUNTIME_MS;
  if (*count >= capacity || *count >= CRABIT_USB_SCRIPT_MAX_ACTIONS) {
    *error = "actions";
    return 0U;
  }
  if (*runtime > CRABIT_USB_SCRIPT_MAX_RUNTIME_MS - cost) {
    *error = "budget";
    return 0U;
  }
  actions[*count].delay_ms = delay_ms;
  actions[*count].modifier = modifier;
  actions[*count].key = key;
  ++*count;
  *runtime += cost;
  return 1U;
}

static uint8_t map_character(char ch, uint8_t *modifier, uint8_t *key)
{
  *modifier = 0U;
  if (ch >= 'a' && ch <= 'z') {
    *key = (uint8_t)(0x04U + (uint8_t)(ch - 'a'));
    return 1U;
  }
  if (ch >= 'A' && ch <= 'Z') {
    *key = (uint8_t)(0x04U + (uint8_t)(ch - 'A'));
    *modifier = 0x02U;
    return 1U;
  }
  if (ch >= '1' && ch <= '9') {
    *key = (uint8_t)(0x1EU + (uint8_t)(ch - '1'));
    return 1U;
  }
  if (ch == '0') {
    *key = 0x27U;
    return 1U;
  }

  switch (ch) {
    case ' ': *key = 0x2CU; break;
    case '!': *key = 0x1EU; *modifier = 0x02U; break;
    case '"': *key = 0x34U; *modifier = 0x02U; break;
    case '#': *key = 0x20U; *modifier = 0x02U; break;
    case '$': *key = 0x21U; *modifier = 0x02U; break;
    case '%': *key = 0x22U; *modifier = 0x02U; break;
    case '&': *key = 0x24U; *modifier = 0x02U; break;
    case '\'': *key = 0x34U; break;
    case '(': *key = 0x26U; *modifier = 0x02U; break;
    case ')': *key = 0x27U; *modifier = 0x02U; break;
    case '*': *key = 0x25U; *modifier = 0x02U; break;
    case '+': *key = 0x2EU; *modifier = 0x02U; break;
    case ',': *key = 0x36U; break;
    case '-': *key = 0x2DU; break;
    case '.': *key = 0x37U; break;
    case '/': *key = 0x38U; break;
    case ':': *key = 0x33U; *modifier = 0x02U; break;
    case ';': *key = 0x33U; break;
    case '<': *key = 0x36U; *modifier = 0x02U; break;
    case '=': *key = 0x2EU; break;
    case '>': *key = 0x37U; *modifier = 0x02U; break;
    case '?': *key = 0x38U; *modifier = 0x02U; break;
    case '@': *key = 0x1FU; *modifier = 0x02U; break;
    case '[': *key = 0x2FU; break;
    case '\\': *key = 0x31U; break;
    case ']': *key = 0x30U; break;
    case '^': *key = 0x23U; *modifier = 0x02U; break;
    case '_': *key = 0x2DU; *modifier = 0x02U; break;
    case '`': *key = 0x35U; break;
    case '{': *key = 0x2FU; *modifier = 0x02U; break;
    case '|': *key = 0x31U; *modifier = 0x02U; break;
    case '}': *key = 0x30U; *modifier = 0x02U; break;
    case '~': *key = 0x35U; *modifier = 0x02U; break;
    default: return 0U;
  }
  return 1U;
}

static uint8_t span_equals(const char *start, size_t length, const char *word)
{
  return (uint8_t)(strlen(word) == length && memcmp(start, word, length) == 0);
}

static uint8_t named_key(const char *start, size_t length, uint8_t *key)
{
  static const struct {
    const char *name;
    uint8_t usage;
  } names[] = {
    {"ENTER", 0x28U}, {"TAB", 0x2BU}, {"ESC", 0x29U},
    {"BACKSPACE", 0x2AU}, {"SPACE", 0x2CU}, {"UP", 0x52U},
    {"DOWN", 0x51U}, {"LEFT", 0x50U}, {"RIGHT", 0x4FU},
    {"DELETE", 0x4CU}, {"HOME", 0x4AU}, {"END", 0x4DU}
  };
  for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
    if (span_equals(start, length, names[i].name)) {
      *key = names[i].usage;
      return 1U;
    }
  }
  return 0U;
}

static uint8_t parse_key_line(const char *line, size_t length,
                              CrabitUsbKeyAction *actions, uint16_t capacity,
                              uint16_t *count, uint32_t *runtime,
                              const char **error)
{
  size_t start = 0U;
  size_t end = length;
  uint8_t modifiers = 0U;
  uint8_t key = 0U;
  uint8_t have_key = 0U;

  while (start < end && is_hspace(line[start])) ++start;
  while (end > start && is_hspace(line[end - 1U])) --end;
  if (start == end) return 1U;

  for (size_t i = start; i < end; ++i) {
    unsigned char ch = (unsigned char)line[i];
    if (ch < 0x20U || ch > 0x7EU) {
      *error = "ascii";
      return 0U;
    }
  }

  if (end - start >= 5U && memcmp(line + start, "DELAY", 5U) == 0) {
    size_t number_start = start + 5U;
    uint32_t delay;
    if (number_start == end || !is_hspace(line[number_start])) {
      *error = "syntax";
      return 0U;
    }
    while (number_start < end && is_hspace(line[number_start])) ++number_start;
    if (!parse_decimal(line + number_start, end - number_start,
                       CRABIT_USB_SCRIPT_MAX_RUNTIME_MS, &delay)) {
      *error = "delay";
      return 0U;
    }
    return append_action(actions, capacity, count, runtime, (uint16_t)delay,
                         0U, 0U, error);
  }

  while (start < end) {
    size_t token_end = start;
    uint8_t modifier = 0U;
    while (token_end < end && line[token_end] != '+' && !is_hspace(line[token_end])) ++token_end;
    if (token_end == start) {
      *error = "syntax";
      return 0U;
    }
    size_t token_length = token_end - start;
    if (span_equals(line + start, token_length, "CTRL")) modifier = 0x01U;
    else if (span_equals(line + start, token_length, "ALT")) modifier = 0x04U;
    else if (span_equals(line + start, token_length, "SHIFT")) modifier = 0x02U;
    else if (span_equals(line + start, token_length, "GUI")) modifier = 0x08U;
    if (modifier != 0U) {
      if (have_key || (modifiers & modifier) != 0U) {
        *error = "modifier";
        return 0U;
      }
      modifiers |= modifier;
    } else {
      if (have_key) {
        *error = "key";
        return 0U;
      }
      if (token_length == 1U) {
        uint8_t implicit_modifier;
        if (!map_character(line[start], &implicit_modifier, &key) ||
            (line[start] == ' ')) {
          *error = "key";
          return 0U;
        }
        // Named modifier chords use letter key usages, not STRING casing.
        if (modifiers && line[start] >= 'A' && line[start] <= 'Z') implicit_modifier = 0U;
        modifiers |= implicit_modifier;
      } else if (!named_key(line + start, token_length, &key)) {
        *error = "key";
        return 0U;
      }
      have_key = 1U;
    }
    if (token_end == end) break;
    start = token_end + 1U;
    while (start < end && is_hspace(line[start])) ++start;
    if (start == end) {
      *error = "syntax";
      return 0U;
    }
  }
  if (!have_key) {
    *error = "key";
    return 0U;
  }
  return append_action(actions, capacity, count, runtime, 0U, modifiers, key,
                       error);
}

static uint8_t parse_string_line(const char *line, size_t length,
                                 CrabitUsbKeyAction *actions,
                                 uint16_t capacity, uint16_t *count,
                                 uint32_t *runtime, const char **error)
{
  size_t start = 0U;
  while (start < length && is_hspace(line[start])) ++start;
  if (length - start < 7U || memcmp(line + start, "STRING ", 7U) != 0) {
    return 2U; /* Not a STRING directive. */
  }
  if (start + 7U == length) {
    *error = "string";
    return 0U;
  }
  for (size_t i = start + 7U; i < length; ++i) {
    uint8_t modifier;
    uint8_t key;
    unsigned char ch = (unsigned char)line[i];
    if (ch < 0x20U || ch > 0x7EU) {
      *error = "ascii";
      return 0U;
    }
    if (!map_character(line[i], &modifier, &key)) {
      *error = "key";
      return 0U;
    }
    if (!append_action(actions, capacity, count, runtime, 0U, modifier, key,
                       error)) return 0U;
  }
  return 1U;
}

static uint8_t parse_line(const char *line, size_t length,
                          CrabitUsbKeyAction *actions, uint16_t capacity,
                          uint16_t *count, uint32_t *runtime,
                          const char **error)
{
  uint8_t string_result = parse_string_line(line, length, actions, capacity,
                                             count, runtime, error);
  if (string_result != 2U) return string_result;
  return parse_key_line(line, length, actions, capacity, count, runtime, error);
}

uint8_t CrabitUsb_ScriptProgramParse(const char *script, size_t length,
                                     CrabitUsbKeyAction *actions,
                                     uint16_t action_capacity,
                                     uint16_t *action_count,
                                     uint32_t *runtime_ms,
                                     const char **error)
{
  uint16_t count = 0U;
  uint32_t runtime = 0U;
  size_t offset = 0U;
  const char *reason = "none";
  uint8_t success = 0U;

  if (action_count != NULL) *action_count = 0U;
  if (runtime_ms != NULL) *runtime_ms = 0U;
  if (error != NULL) *error = "none";
  if (script == NULL || actions == NULL || action_count == NULL ||
      runtime_ms == NULL || error == NULL || action_capacity == 0U) {
    reason = "null";
    goto done;
  }
  if (length == 0U) {
    reason = "empty";
    goto done;
  }
  if (length > CRABIT_USB_SCRIPT_MAX_BYTES) {
    reason = "length";
    goto done;
  }
  if (action_capacity > CRABIT_USB_SCRIPT_MAX_ACTIONS) {
    action_capacity = CRABIT_USB_SCRIPT_MAX_ACTIONS;
  }

  while (offset < length) {
    size_t line_start = offset;
    size_t line_end;
    uint8_t has_newline;
    while (offset < length && script[offset] != '\n') ++offset;
    line_end = offset;
    has_newline = (uint8_t)(offset < length);
    if (has_newline) ++offset;
    if (has_newline && line_end > line_start && script[line_end - 1U] == '\r') {
      --line_end;
    }
    if (line_end - line_start > CRABIT_USB_SCRIPT_MAX_LINE_BYTES) {
      reason = "line";
      goto done;
    }
    if (!parse_line(script + line_start, line_end - line_start, actions,
                    action_capacity, &count, &runtime, &reason)) goto done;
  }
  if (count == 0U) {
    reason = "empty";
    goto done;
  }
  success = 1U;

done:
  if (success) {
    *action_count = count;
    *runtime_ms = runtime;
    *error = "none";
  } else {
    *action_count = 0U;
    *runtime_ms = 0U;
    *error = reason;
  }
  return success;
}
