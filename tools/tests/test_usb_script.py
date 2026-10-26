"""Host tests for the bounded STM32 USB script parser and UI-task service."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPOSITE = ROOT / "firmware/stm32/USB_DEVICE/Composite"
PARSER_H = COMPOSITE / "usb_script_program.h"
PARSER_C = COMPOSITE / "usb_script_program.c"
SERVICE_H = COMPOSITE / "usb_script.h"
SERVICE_C = COMPOSITE / "usb_script.c"
KEYBOARD_H = COMPOSITE / "usb_keyboard.h"


HARNESS = r"""#include "usb_script.h"
#include "usb_script_program.h"
#include "usb_keyboard.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "check failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
    exit(2); \
  } \
} while (0)

static uint32_t fake_tick;
static uint8_t fake_busy, fake_available;
static uint8_t fake_run_result;
static uint8_t fake_cancel_count;
static uint16_t fake_run_count, fake_action_count;
static uint32_t fake_budget;
static CrabitUsb_KeyboardState fake_status;
static const CrabitUsbKeyAction *fake_actions;

uint32_t HAL_GetTick(void) { return fake_tick; }
CrabitUsb_KeyboardState CrabitUsb_KeyboardStatus(void) { return fake_status; }
uint8_t CrabitUsb_KeyboardBusy(void) { return fake_busy; }
uint8_t CrabitUsb_KeyboardAvailable(void) { return fake_available; }

uint8_t CrabitUsb_KeyboardRun(const CrabitUsbKeyAction *actions,
                              uint16_t count, uint32_t budget_ms)
{
  ++fake_run_count;
  fake_actions = actions;
  fake_action_count = count;
  fake_budget = budget_ms;
  if (fake_run_result == USBD_OK) {
    fake_busy = 1U;
    fake_status = CRABIT_KEYBOARD_RUNNING;
  }
  return fake_run_result;
}

void CrabitUsb_KeyboardCancel(void)
{
  ++fake_cancel_count;
  fake_status = CRABIT_KEYBOARD_FAILED;
  /* The in-flight all-up report keeps Busy set until the test completes it. */
}

static void expect_contains(const char *reply, const char *part)
{
  if (strlen(reply) > 191U || reply[191] != '\0') {
    fprintf(stderr, "reply exceeded the bounded response: %s\n", reply);
    exit(2);
  }
  if (strstr(reply, part) == NULL) {
    fprintf(stderr, "reply missing [%s]: %s\n", part, reply);
    exit(2);
  }
}

static void upload(uint32_t id, const char *script, size_t length)
{
  char command[160];
  char reply[192];
  int n = snprintf(command, sizeof(command), "begin:%08lx:%u",
                   (unsigned long)id, (unsigned int)length);
  assert(n > 0 && (size_t)n < sizeof(command));
  CrabitUsb_ScriptHandle(command, (size_t)n, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading");
  for (size_t offset = 0U; offset < length;) {
    size_t bytes = length - offset;
    if (bytes > 64U) bytes = 64U;
    n = snprintf(command, sizeof(command), "chunk:%08lx:%u:",
                 (unsigned long)id, (unsigned int)offset);
    assert(n > 0 && (size_t)n + bytes * 2U < sizeof(command));
    for (size_t i = 0U; i < bytes; ++i) {
      static const char hex[] = "0123456789abcdef";
      uint8_t byte = (uint8_t)script[offset + i];
      command[n++] = hex[byte >> 4];
      command[n++] = hex[byte & 0x0FU];
    }
    command[n] = '\0';
    CrabitUsb_ScriptHandle(command, (size_t)n, reply, sizeof(reply));
    expect_contains(reply, "result=accepted;state=uploading");
    offset += bytes;
  }
  n = snprintf(command, sizeof(command), "commit:%08lx", (unsigned long)id);
  assert(n > 0 && (size_t)n < sizeof(command));
  CrabitUsb_ScriptHandle(command, (size_t)n, reply, sizeof(reply));
  expect_contains(reply, "result=ready;state=ready");
  expect_contains(reply, "error=none");
}

static void parser_case(void)
{
  static const char punctuation[] = " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  static const uint8_t usages[] = {
    0x2C,0x1E,0x34,0x20,0x21,0x22,0x24,0x34,0x26,0x27,0x25,
    0x2E,0x36,0x2D,0x37,0x38,0x33,0x33,0x36,0x2E,0x37,0x38,
    0x1F,0x2F,0x31,0x30,0x23,0x2D,0x35,0x2F,0x31,0x30,0x35
  };
  static const uint8_t shifted[] = {
    0,1,1,1,1,1,1,0,1,1,1,1,0,0,0,0,1,0,1,0,1,1,
    1,0,0,0,1,1,0,1,1,1,1
  };
  static const uint8_t named_usages[] = {
    0x28,0x2B,0x29,0x2A,0x2C,0x52,0x51,0x50,0x4F,0x4C,0x4A,0x4D
  };
  CrabitUsbKeyAction actions[CRABIT_USB_SCRIPT_MAX_ACTIONS];
  uint16_t count = 0U;
  uint32_t runtime = 0U;
  const char *error = NULL;
  char script[160];
  int n = snprintf(script, sizeof(script),
                   "\r\nSTRING %s\r\nCTRL+ALT+SHIFT+GUI+z\n"
                   "CTRL+ALT+DELETE\nENTER\nTAB\nESC\nBACKSPACE\nSPACE\nUP\nDOWN\n"
                   "LEFT\nRIGHT\nDELETE\nHOME\nEND\nDELAY 0\n",
                   punctuation);
  assert(n > 0 && (size_t)n < sizeof(script));
  assert(CrabitUsb_ScriptProgramParse(script, (size_t)n, actions,
                                      CRABIT_USB_SCRIPT_MAX_ACTIONS, &count,
                                      &runtime, &error));
  assert(error != NULL && strcmp(error, "none") == 0);
  assert(count == (uint16_t)(sizeof(usages) + 15U));
  for (size_t i = 0U; i < sizeof(usages); ++i) {
    assert(actions[i].key == usages[i]);
    assert(actions[i].modifier == (shifted[i] ? 0x02U : 0U));
  }
  assert(actions[sizeof(usages)].modifier == 0x0FU);
  assert(actions[sizeof(usages)].key == 0x1DU);
  assert(actions[sizeof(usages) + 1U].modifier == 0x05U);
  assert(actions[sizeof(usages) + 1U].key == 0x4CU);
  for (size_t i = 0U; i < sizeof(named_usages); ++i) {
    assert(actions[sizeof(usages) + 2U + i].key == named_usages[i]);
  }
  assert(actions[count - 1U].delay_ms == 0U && actions[count - 1U].key == 0U);
  assert(runtime == (uint32_t)(count - 1U) * 40U);

  assert(CrabitUsb_ScriptProgramParse("STRING AaZ09", 12U, actions, 128U,
                                      &count, &runtime, &error));
  assert(count == 5U && runtime == 200U);
  assert(actions[0].key == 0x04U && actions[0].modifier == 0x02U);
  assert(actions[1].key == 0x04U && actions[1].modifier == 0U);
  assert(actions[2].key == 0x1DU && actions[2].modifier == 0x02U);
  assert(actions[3].key == 0x27U && actions[4].key == 0x26U);
  static const char chords[] = "CTRL A\nGUI r";
  assert(CrabitUsb_ScriptProgramParse(chords, sizeof(chords)-1U, actions, 128U,
                                      &count, &runtime, &error));
  assert(count == 2U && runtime == 80U);
  assert(actions[0].key == 4U && actions[0].modifier == 1U);
  assert(actions[1].key == 21U && actions[1].modifier == 8U);
}

static void parser_bounds_case(void)
{
  CrabitUsbKeyAction actions[CRABIT_USB_SCRIPT_MAX_ACTIONS];
  uint16_t count;
  uint32_t runtime;
  const char *error;
  char script[CRABIT_USB_SCRIPT_MAX_BYTES + 2U];
  size_t used = 0U;

  for (unsigned i = 0U; i < 64U; ++i) {
    memcpy(script + used, "DELAY 0\n", 8U);
    used += 8U;
  }
  assert(used == CRABIT_USB_SCRIPT_MAX_BYTES);
  assert(CrabitUsb_ScriptProgramParse(script, used, actions, 128U, &count,
                                      &runtime, &error));
  assert(count == 64U && runtime == 0U);

  script[used] = '\n';
  assert(!CrabitUsb_ScriptProgramParse(script, used + 1U, actions, 128U,
                                       &count, &runtime, &error));
  assert(strcmp(error, "length") == 0 && count == 0U && runtime == 0U);

  for (unsigned i = 0U; i < 128U; ++i) {
    script[i * 2U] = 'A';
    script[i * 2U + 1U] = '\n';
  }
  assert(CrabitUsb_ScriptProgramParse(script, 256U, actions, 128U, &count,
                                      &runtime, &error));
  assert(count == 128U && runtime == 5120U);
  script[256] = 'A';
  script[257] = '\n';
  assert(!CrabitUsb_ScriptProgramParse(script, 258U, actions, 128U, &count,
                                       &runtime, &error));
  assert(strcmp(error, "actions") == 0);

  assert(CrabitUsb_ScriptProgramParse("DELAY 30000", 11U, actions, 128U,
                                      &count, &runtime, &error));
  assert(count == 1U && runtime == 30000U);
  assert(!CrabitUsb_ScriptProgramParse("DELAY 30000\nDELAY 1", 19U,
                                       actions, 128U, &count, &runtime,
                                       &error));
  assert(strcmp(error, "budget") == 0);

  memset(script, ' ', 129U);
  assert(!CrabitUsb_ScriptProgramParse(script, 129U, actions, 128U, &count,
                                       &runtime, &error));
  assert(strcmp(error, "line") == 0);
}

static void parser_reject_case(void)
{
  CrabitUsbKeyAction actions[CRABIT_USB_SCRIPT_MAX_ACTIONS];
  uint16_t count;
  uint32_t runtime;
  const char *error;
  static const char *bad[] = {
    "STRING ", "STRING hi\tthere", "STRING caf\xC3\xA9", "DELAY -1",
    "DELAY 30001", "CTRL+ALT+", "CTRL+CTRL+a", "CTRL+a+b",
    "CTRL+UNKNOWN", "F1", "UNKNOWN", "CTRL+GUI+F10"
  };
  for (size_t i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    assert(!CrabitUsb_ScriptProgramParse(bad[i], strlen(bad[i]), actions,
                                         128U, &count, &runtime, &error));
    assert(error != NULL && strcmp(error, "none") != 0);
    assert(count == 0U && runtime == 0U);
  }
  assert(!CrabitUsb_ScriptProgramParse(" \n\t\n", 4U, actions, 128U,
                                       &count, &runtime, &error));
  assert(strcmp(error, "empty") == 0);
}

static void upload_atomic_case(void)
{
  char reply[192];
  CrabitUsb_ScriptHandle("status", 6U, reply, sizeof(reply));
  expect_contains(reply, "result=snapshot;state=empty;id=00000000");

  CrabitUsb_ScriptHandle("begin:00000001:12", 17U, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading;id=00000001;received=0;length=12");
  CrabitUsb_ScriptHandle("chunk:00000001:1:4142", 21U, reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=uploading;id=00000001;received=0");
  CrabitUsb_ScriptHandle("chunk:00000001:0:4x", 19U, reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=uploading;id=00000001;received=0");
  CrabitUsb_ScriptHandle("chunk:00000001:0:535452494e472078", 33U,
                         reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading;id=00000001;received=8");
  CrabitUsb_ScriptHandle("chunk:00000001:7:0a", 19U, reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=uploading;id=00000001;received=8");
  CrabitUsb_ScriptHandle("commit:00000001", 15U, reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=uploading");
  expect_contains(reply, "error=incomplete");
  CrabitUsb_ScriptHandle("chunk:00000001:8:4869", 21U, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading;id=00000001;received=10");
  CrabitUsb_ScriptHandle("chunk:00000001:10:0a", 20U, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading;id=00000001;received=11");
  CrabitUsb_ScriptHandle("chunk:00000001:11:0a", 20U, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading;id=00000001;received=12");
  CrabitUsb_ScriptHandle("commit:00000001", 15U, reply, sizeof(reply));
  expect_contains(reply, "result=ready;state=ready");
  expect_contains(reply, "error=none");
}

static void timeout_wrap_case(void)
{
  char reply[192];
  fake_tick = UINT32_MAX - 10000U;
  CrabitUsb_ScriptHandle("begin:00000021:4", 16U, reply, sizeof(reply));
  expect_contains(reply, "state=uploading");
  fake_tick += 29999U;
  CrabitUsb_ScriptHandle("status", 6U, reply, sizeof(reply));
  expect_contains(reply, "state=uploading;id=00000021");
  fake_tick += 1U;
  CrabitUsb_ScriptHandle("status", 6U, reply, sizeof(reply));
  expect_contains(reply, "result=snapshot;state=empty;id=00000000");
  expect_contains(reply, "error=timeout");
}

static void run_once_stop_case(void)
{
  char reply[192];
  char command[32];
  static const char script[] = "STRING A";
  upload(0x1234ABCDU, script, sizeof(script) - 1U);
  fake_available = 1U;
  fake_run_result = USBD_OK;
  snprintf(command, sizeof(command), "run:%08x", 0x1234ABCDU);
  CrabitUsb_ScriptHandle(command, strlen(command), reply, sizeof(reply));
  expect_contains(reply, "result=queued;state=running");
  assert(fake_run_count == 1U && fake_action_count == 1U);
  assert(fake_actions != NULL && fake_actions[0].key == 0x04U);
  assert(fake_budget == 2040U);

  CrabitUsb_ScriptHandle("clear:1234abcd", 14U, reply, sizeof(reply));
  expect_contains(reply, "result=busy;state=running");
  CrabitUsb_ScriptHandle("begin:00000002:8", 16U, reply, sizeof(reply));
  expect_contains(reply, "result=busy;state=running");
  assert(fake_actions[0].key == 0x04U && fake_actions[0].modifier == 0x02U);

  CrabitUsb_ScriptHandle("stop:1234abcd", 13U, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=stopping");
  assert(fake_cancel_count == 1U && fake_busy == 1U);
  CrabitUsb_ScriptHandle("clear:1234abcd", 14U, reply, sizeof(reply));
  expect_contains(reply, "result=busy;state=stopping");
  assert(fake_actions[0].key == 0x04U && fake_actions[0].modifier == 0x02U);
  fake_busy = 0U;
  CrabitUsb_ScriptHandle("status", 6U, reply, sizeof(reply));
  expect_contains(reply, "state=stopped;id=1234abcd");
  CrabitUsb_ScriptHandle("clear:1234abcd", 14U, reply, sizeof(reply));
  expect_contains(reply, "result=cleared;state=empty");
  snprintf(command, sizeof(command), "begin:%08x:8", 0x1234ABCDU);
  CrabitUsb_ScriptHandle(command, strlen(command), reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=empty");
  expect_contains(reply, "error=replay");

  upload(0x1234ABCEU, script, sizeof(script) - 1U);
  fake_status = CRABIT_KEYBOARD_DONE;
  CrabitUsb_ScriptHandle("run:1234abce", 12U, reply, sizeof(reply));
  expect_contains(reply, "result=queued;state=running");
  fake_busy = 0U;
  fake_status = CRABIT_KEYBOARD_DONE;
  CrabitUsb_ScriptHandle("status", 6U, reply, sizeof(reply));
  expect_contains(reply, "state=done;id=1234abce");
  CrabitUsb_ScriptHandle("run:1234abce", 12U, reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=done");
  expect_contains(reply, "error=replay");
}

static void unavailable_and_busy_case(void)
{
  char reply[192];
  static const char script[] = "DELAY 30000";
  upload(0x55U, script, sizeof(script) - 1U);
  CrabitUsb_ScriptHandle("run:00000055", 12U, reply, sizeof(reply));
  expect_contains(reply, "result=unavailable;state=ready");
  expect_contains(reply, "error=usb");
  fake_available = 1U;
  fake_busy = 1U; /* Includes a fixed-test or pending all-up report. */
  CrabitUsb_ScriptHandle("run:00000055", 12U, reply, sizeof(reply));
  expect_contains(reply, "result=busy;state=ready");
  CrabitUsb_ScriptHandle("clear:00000055", 14U, reply, sizeof(reply));
  expect_contains(reply, "result=busy;state=ready");
  fake_busy = 0U;
  fake_status = CRABIT_KEYBOARD_IDLE;
  fake_run_result = USBD_OK;
  CrabitUsb_ScriptHandle("run:00000055", 12U, reply, sizeof(reply));
  expect_contains(reply, "result=queued;state=running");
  assert(fake_budget == 32000U);
}

static void chunk_limit_case(void)
{
  char reply[192];
  char command[160];
  int n = snprintf(command, sizeof(command), "begin:00000031:100");
  assert(n > 0);
  CrabitUsb_ScriptHandle(command, (size_t)n, reply, sizeof(reply));
  expect_contains(reply, "state=uploading");

  n = snprintf(command, sizeof(command), "chunk:00000031:0:");
  for (unsigned i = 0U; i < 65U; ++i) {
    command[n++] = '4';
    command[n++] = '1';
  }
  command[n] = '\0';
  CrabitUsb_ScriptHandle(command, (size_t)n, reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=uploading");
  expect_contains(reply, "received=0");
  expect_contains(reply, "error=hex");

  n = snprintf(command, sizeof(command), "chunk:00000031:0:");
  for (unsigned i = 0U; i < 64U; ++i) {
    command[n++] = '4';
    command[n++] = '1';
  }
  command[n] = '\0';
  CrabitUsb_ScriptHandle(command, (size_t)n, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading");
  expect_contains(reply, "received=64");

  n = snprintf(command, sizeof(command), "chunk:00000031:64:");
  for (unsigned i = 0U; i < 37U; ++i) {
    command[n++] = '4';
    command[n++] = '1';
  }
  command[n] = '\0';
  CrabitUsb_ScriptHandle(command, (size_t)n, reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=uploading");
  expect_contains(reply, "received=64");
  expect_contains(reply, "error=length");

  n = snprintf(command, sizeof(command), "chunk:00000031:64:");
  for (unsigned i = 0U; i < 36U; ++i) {
    command[n++] = '4';
    command[n++] = '1';
  }
  command[n] = '\0';
  CrabitUsb_ScriptHandle(command, (size_t)n, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading");
  expect_contains(reply, "received=100");
}

static void run_return_case(void)
{
  char reply[192];
  upload(0x66U, "STRING A", 8U);
  fake_available = 1U;
  fake_run_result = USBD_BUSY;
  CrabitUsb_ScriptHandle("run:00000066", 12U, reply, sizeof(reply));
  expect_contains(reply, "result=busy;state=ready");
  assert(fake_run_count == 1U);

  fake_run_result = USBD_FAIL;
  CrabitUsb_ScriptHandle("run:00000066", 12U, reply, sizeof(reply));
  expect_contains(reply, "result=failed;state=failed");
  expect_contains(reply, "error=driver");
  assert(fake_run_count == 2U);
}

static void malformed_and_parse_failure_case(void)
{
  char reply[192];
  CrabitUsb_ScriptHandle("begin:00000000:1", 16U, reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=empty;id=00000000;received=0;length=0;error=id");
  CrabitUsb_ScriptHandle("begin:00000001:513", 18U, reply, sizeof(reply));
  expect_contains(reply, "result=invalid;state=empty");
  expect_contains(reply, "error=length");
  CrabitUsb_ScriptHandle("begin:00000001:3", 16U, reply, sizeof(reply));
  CrabitUsb_ScriptHandle("chunk:00000001:0:41420a", 23U, reply, sizeof(reply));
  expect_contains(reply, "received=3");
  CrabitUsb_ScriptHandle("commit:00000001", 15U, reply, sizeof(reply));
  expect_contains(reply, "result=failed;state=failed");
  expect_contains(reply, "error=key");
  assert(fake_run_count == 0U);

  CrabitUsb_ScriptHandle("clear:00000001", 14U, reply, sizeof(reply));
  expect_contains(reply, "result=cleared;state=empty");
  CrabitUsb_ScriptHandle("begin:00000002:1", 16U, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading");
  CrabitUsb_ScriptHandle("chunk:00000002:0:7f", 19U, reply, sizeof(reply));
  expect_contains(reply, "result=accepted;state=uploading");
  CrabitUsb_ScriptHandle("commit:00000002", 15U, reply, sizeof(reply));
  expect_contains(reply, "result=failed;state=failed");
  expect_contains(reply, "error=ascii");
}

static void small_reply_case(void)
{
  char reply[8];
  memset(reply, 'X', sizeof(reply));
  CrabitUsb_ScriptHandle("status", 6U, reply, sizeof(reply));
  assert(reply[sizeof(reply) - 1U] == '\0');
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  fake_available = 0U;
  fake_run_result = USBD_OK;
  fake_status = CRABIT_KEYBOARD_IDLE;
  if (strcmp(argv[1], "parser") == 0) parser_case();
  else if (strcmp(argv[1], "parser_bounds") == 0) parser_bounds_case();
  else if (strcmp(argv[1], "parser_reject") == 0) parser_reject_case();
  else if (strcmp(argv[1], "upload_atomic") == 0) upload_atomic_case();
  else if (strcmp(argv[1], "timeout_wrap") == 0) timeout_wrap_case();
  else if (strcmp(argv[1], "run_once_stop") == 0) run_once_stop_case();
  else if (strcmp(argv[1], "unavailable_busy") == 0) unavailable_and_busy_case();
  else if (strcmp(argv[1], "chunk_limit") == 0) chunk_limit_case();
  else if (strcmp(argv[1], "run_return") == 0) run_return_case();
  else if (strcmp(argv[1], "malformed_parse_failure") == 0) malformed_and_parse_failure_case();
  else if (strcmp(argv[1], "small_reply") == 0) small_reply_case();
  else exit(4);
  printf("PASS %s\n", argv[1]);
  return 0;
}
"""


FAKE_HID = r"""#ifndef TEST_USBD_CUSTOMHID_H
#define TEST_USBD_CUSTOMHID_H
#include <stdint.h>
#define USBD_OK 0
#define USBD_BUSY 1
#define USBD_FAIL 2
typedef struct { uint8_t unused; } USBD_ClassTypeDef;
typedef struct { uint8_t unused; } USBD_CUSTOM_HID_ItfTypeDef;
#endif
"""

FAKE_HAL = r"""#ifndef TEST_STM32L4XX_HAL_H
#define TEST_STM32L4XX_HAL_H
#include <stdint.h>
uint32_t HAL_GetTick(void);
#endif
"""


class UsbScriptHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        candidates = [Path("C:/w64dev/w64devkit/bin/gcc.exe"), Path("C:/msys64/ucrt64/bin/gcc.exe")]
        found = shutil.which("gcc")
        if found:
            candidates.append(Path(found))
        compiler = next((path for path in candidates if path.is_file()), None)
        if compiler is None:
            raise AssertionError("existing native GCC unavailable; host test was not run")

        cls.temp = tempfile.TemporaryDirectory(prefix="crabit-usb-script-")
        cls.addClassCleanup(cls.temp.cleanup)
        temp = Path(cls.temp.name)
        (temp / "usbd_customhid.h").write_text(FAKE_HID, encoding="ascii")
        (temp / "stm32l4xx_hal.h").write_text(FAKE_HAL, encoding="ascii")
        (temp / "harness.c").write_text(HARNESS, encoding="ascii")
        cls.binary = temp / "usb-script-host.exe"

        sources = [PARSER_C, SERVICE_C, temp / "harness.c"]
        cls.source_hashes = {
            path: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (PARSER_H, PARSER_C, SERVICE_H, SERVICE_C, KEYBOARD_H)
        }
        command = [
            str(compiler),
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O0",
            "-g",
            "-I",
            str(temp),
            "-I",
            str(COMPOSITE),
            *(str(source) for source in sources),
            "-o",
            str(cls.binary),
        ]
        cls.environment = os.environ.copy()
        cls.environment["PATH"] = (
            str(compiler.parent) + os.pathsep + cls.environment.get("PATH", "")
        )
        result = subprocess.run(
            command, cwd=ROOT, env=cls.environment, capture_output=True, text=True, timeout=60
        )
        if result.returncode:
            raise AssertionError("native C compile failed:\n" + result.stdout + result.stderr)
        cls.assert_sources_stable()

    @classmethod
    def assert_sources_stable(cls):
        for path, expected in cls.source_hashes.items():
            if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise AssertionError(f"source changed during native fixture: {path}")

    def run_case(self, name):
        self.assert_sources_stable()
        try:
            result = subprocess.run(
                [str(self.binary), name],
                cwd=ROOT,
                env=self.environment,
                capture_output=True,
                text=True,
                timeout=4,
            )
        except subprocess.TimeoutExpired as exc:
            self.fail(f"native case {name} stalled: {exc.stdout!r} {exc.stderr!r}")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS {name}", result.stdout)

    def test_parser_us_ascii_mapping_and_named_keys(self):
        self.run_case("parser")

    def test_parser_limits_and_runtime_overflow(self):
        self.run_case("parser_bounds")

    def test_parser_rejects_malformed_input(self):
        self.run_case("parser_reject")

    def test_upload_is_sequential_and_rejections_are_atomic(self):
        self.run_case("upload_atomic")

    def test_upload_timeout_handles_tick_wrap(self):
        self.run_case("timeout_wrap")

    def test_one_shot_run_and_pending_stop(self):
        self.run_case("run_once_stop")

    def test_usb_unavailable_and_keyboard_busy_guards(self):
        self.run_case("unavailable_busy")

    def test_chunk_decoded_length_and_upload_overflow(self):
        self.run_case("chunk_limit")

    def test_run_requires_successful_backend_acceptance(self):
        self.run_case("run_return")

    def test_bad_command_and_script_commit_failure(self):
        self.run_case("malformed_parse_failure")

    def test_reply_is_terminated_for_small_buffers(self):
        self.run_case("small_reply")

    def test_service_has_no_flash_or_legacy_badusb_dependency(self):
        source = SERVICE_C.read_text(encoding="utf-8")
        self.assertNotRegex(source, r"HAL_FLASH|FLASH_(?:Erase|Program)|BadUsb")
        self.assertNotIn("badusb", source.lower())


if __name__ == "__main__":
    unittest.main()
