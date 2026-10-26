#include "usb_diag.h"
#include "usb_keyboard.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>

extern volatile uint8_t bw16_flash_mode;
enum { LINE_SIZE = 64, QUEUE_SIZE = 4 };
static char assembling[LINE_SIZE], queue[QUEUE_SIZE][LINE_SIZE];
static uint8_t used, discarding;
static volatile uint8_t head, tail, count, overflow;
static volatile uint32_t epoch;
/* Only the UI task owns these reply fields. IRQ reset invalidates via epoch. */
static char reply[112];
static uint16_t reply_length;
static uint32_t reply_epoch, reply_started;

void CrabitUsb_DiagReset(void)
{
  used = discarding = head = tail = count = overflow = 0;
  ++epoch;
}

void CrabitUsb_DiagReceive(const uint8_t *bytes, uint32_t length)
{
  if (bw16_flash_mode) return;
  for (uint32_t i = 0; i < length; ++i) {
    uint8_t byte = bytes[i];
    if (byte == '\n') {
      if (!discarding && used) {
        assembling[used] = '\0';
        if (count < QUEUE_SIZE) {
          memcpy(queue[head], assembling, used + 1U);
          head = (head + 1U) % QUEUE_SIZE;
          ++count;
        } else overflow = 1;
      }
      used = discarding = 0;
    } else if (byte != '\r') {
      if (byte < 32 || byte > 126 || used == LINE_SIZE - 1U) {
        discarding = 1;
        overflow = 1;
      } else if (!discarding) assembling[used++] = (char)byte;
    }
  }
}

static uint8_t valid_nonce(const char *nonce)
{
  if (strlen(nonce) != 8U) return 0;
  for (unsigned i = 0; i < 8U; ++i) {
    char c = nonce[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) return 0;
  }
  return 1;
}

static void prepare_reply(char *line)
{
  static const char *const states[] = {"IDLE", "RUNNING", "DONE", "FAILED"};
  char *nonce = strchr(line, ' ');
  if (nonce == NULL || !valid_nonce(nonce + 1)) return;
  *nonce++ = '\0';
  int size = 0;
  if (!strcmp(line, "CRABIT:USB:STATUS")) {
    size = snprintf(reply, sizeof(reply),
                    "CRABIT:USB:STATUS %s cdc=1 hid=1 test=%s\n", nonce,
                    states[CrabitUsb_KeyboardStatus()]);
  } else if (!strcmp(line, "CRABIT:USB:ECHO")) {
    size = snprintf(reply, sizeof(reply), "CRABIT:USB:ECHO %s\n", nonce);
  } else if (!strcmp(line, "CRABIT:USB:TYPE")) {
    uint8_t result = CrabitUsb_KeyboardRequest();
    size = snprintf(reply, sizeof(reply), "CRABIT:USB:TYPE %s %s\n", nonce,
                    result == USBD_OK ? "QUEUED" : result == USBD_BUSY ? "BUSY" : "REJECTED");
  }
  if (size > 0 && (unsigned)size < sizeof(reply)) reply_length = (uint16_t)size;
}

void CrabitUsb_DiagTick(void)
{
  char line[LINE_SIZE] = {0};
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (bw16_flash_mode) {
    CrabitUsb_DiagReset();
    reply_length = 0;
    __set_PRIMASK(mask);
    return;
  }
  if (reply_epoch != epoch) reply_length = 0;
  reply_epoch = epoch;
  if (!reply_length) {
    if (overflow) {
      overflow = 0;
      memcpy(line, "!OVERFLOW", 10);
    } else if (count) {
      memcpy(line, queue[tail], LINE_SIZE);
      tail = (tail + 1U) % QUEUE_SIZE;
      --count;
    }
  }
  __set_PRIMASK(mask);
  if (line[0]) {
    /* A reset between dequeue and processing must not execute an old TYPE.
     * Bounded formatting only; no waits, RTOS calls or arbitrary user text. */
    mask = __get_PRIMASK();
    __disable_irq();
    if (reply_epoch != epoch || bw16_flash_mode) {
      __set_PRIMASK(mask);
      return;
    }
    if (!strcmp(line, "!OVERFLOW")) {
      strcpy(reply, "CRABIT:USB:ERROR OVERFLOW\n");
      reply_length = (uint16_t)strlen(reply);
    } else prepare_reply(line);
    reply_started = HAL_GetTick();
    __set_PRIMASK(mask);
  }
  mask = __get_PRIMASK();
  __disable_irq();
  if (reply_epoch != epoch || bw16_flash_mode) reply_length = 0;
  if (reply_length) {
    uint8_t result = CDC_Transmit_FS((uint8_t *)reply, reply_length);
    if (result == USBD_OK || (uint32_t)(HAL_GetTick() - reply_started) >= 2000U) {
      reply_length = 0;
    }
  }
  __set_PRIMASK(mask);
}
