/* Actual product state machines; only HAL, transport and registration are faked. */
#include <stdio.h>
#include <stdlib.h>
#include "usb_keyboard.h"
#include "usb_diag.h"
#include "usb_composite.h"
#include "usb_composite_config.h"
#include "usbd_cdc_if.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
USBD_HandleTypeDef hUsbDeviceFS;
volatile uint8_t bw16_flash_mode;
USBD_ClassTypeDef USBD_CUSTOM_HID, USBD_CDC;
USBD_CDC_ItfTypeDef USBD_Interface_fops_FS;
static USBD_CUSTOM_HID_HandleTypeDef hid;
static uint32_t tick, primask;
static unsigned irq_restores, reset_on_restore;
static const uint8_t *pending;
static uint8_t pending_copy[8], reports[96][8];
static uint32_t report_times[96];
static unsigned report_count, rearmed, send_busy;
static uint8_t cdc_result;
static unsigned cdc_calls, reply_count;
static char replies[64][128];
static unsigned register_calls, register_fail;

uint32_t HAL_GetTick(void) { return tick; }
uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1; }
void __set_PRIMASK(uint32_t value) {
  primask = value;
  ++irq_restores;
  if (reset_on_restore && irq_restores == reset_on_restore) CrabitUsb_DiagReset();
}
uint8_t USBD_CUSTOM_HID_SendReport(USBD_HandleTypeDef *d, uint8_t *p, uint16_t n, uint8_t id) {
  CHECK(d == &hUsbDeviceFS && id == CRABIT_USB_HID_CLASS && n == 8);
  CHECK(pending == NULL && hid.state == CUSTOM_HID_IDLE);
  if (send_busy) { --send_busy; return USBD_BUSY; }
  CHECK(report_count < 96);
  pending = p;
  memcpy(pending_copy, p, 8);
  memcpy(reports[report_count], p, 8);
  report_times[report_count++] = tick;
  hid.state = CUSTOM_HID_BUSY;
  return USBD_OK;
}
uint8_t USBD_CUSTOM_HID_ReceivePacket(USBD_HandleTypeDef *d) {
  CHECK(d == &hUsbDeviceFS); ++rearmed; return USBD_OK;
}
uint8_t CDC_Transmit_FS(uint8_t *p, uint16_t n) {
  ++cdc_calls;
  if (cdc_result != USBD_OK) return cdc_result;
  /* The actual App wrapper copies into UserTxBufferFS before async IN. */
  CHECK(reply_count < 64 && n < sizeof(replies[0]));
  memcpy(replies[reply_count], p, n);
  replies[reply_count++][n] = 0;
  return USBD_OK;
}
USBD_StatusTypeDef USBD_RegisterClassComposite(USBD_HandleTypeDef *d, USBD_ClassTypeDef *c,
    USBD_CompositeClassTypeDef type, uint8_t *eps) {
  ++register_calls;
  if (register_fail == register_calls) return USBD_FAIL;
  unsigned slot = d->classId;
  CHECK(slot < 2);
  CHECK(type == (slot == 0 ? CLASS_TYPE_CDC : CLASS_TYPE_CHID));
  CHECK(eps[0] == (slot == 0 ? 0x81 : 0x83));
  CHECK(eps[1] == (slot == 0 ? 0x01 : 0x03));
  if (!slot) CHECK(eps[2] == 0x82);
  d->pClass[slot] = c; d->tclasslist[slot].Active = 1;
  USBD_CompositeElementTypeDef *entry=&d->tclasslist[slot];
  entry->ClassId=slot; entry->ClassType=type;
  entry->NumIf=slot==0 ? 2 : 1; entry->Ifs[0]=slot==0 ? 0 : 2;
  if (slot==0) entry->Ifs[1]=1;
  entry->NumEps=slot==0 ? 3 : 2;
  for (unsigned i=0;i<entry->NumEps;++i) { entry->Eps[i].add=eps[i]; entry->Eps[i].is_used=1; }
  ++d->classId; ++d->NumClasses;
  return USBD_OK;
}
uint8_t USBD_CDC_RegisterInterface(USBD_HandleTypeDef *d, USBD_CDC_ItfTypeDef *f) {
  CHECK(d->classId == 0 && f == &USBD_Interface_fops_FS);
  return register_fail == 3 ? USBD_FAIL : USBD_OK;
}
uint8_t USBD_CUSTOM_HID_RegisterInterface(USBD_HandleTypeDef *d, USBD_CUSTOM_HID_ItfTypeDef *f) {
  CHECK(d->classId == 1 && f == &CrabitUsb_KeyboardInterface);
  return register_fail == 4 ? USBD_FAIL : USBD_OK;
}
static void complete(void) {
  CHECK(pending != NULL);
  CHECK(memcmp(pending, pending_copy, 8) == 0);
  pending = NULL; hid.state = CUSTOM_HID_IDLE;
}
static void sof(uint32_t now) {
  tick = now;
  CHECK(CrabitUsb_KeyboardClass.SOF(&hUsbDeviceFS) == USBD_OK);
  if (pending) CHECK(memcmp(pending, pending_copy, 8) == 0);
}
static void receive(const char *s) { CrabitUsb_DiagReceive((const uint8_t *)s, (uint32_t)strlen(s)); }
static void drain(unsigned n) { while (n--) CrabitUsb_DiagTick(); }
static void setup(void) {
  memset(&hUsbDeviceFS, 0, sizeof(hUsbDeviceFS));
  memset(&hid, 0, sizeof(hid));
  hUsbDeviceFS.dev_state = USBD_STATE_CONFIGURED;
  hUsbDeviceFS.pClassDataCmsit[CRABIT_USB_HID_CLASS] = &hid;
  CrabitUsb_KeyboardPrepareClass();
  CHECK(CrabitUsb_KeyboardInterface.Init() == USBD_OK);
  CrabitUsb_DiagReset();
  tick = 1000;
}
static void idle(void) {
  for (unsigned i=0; i<100; ++i) sof(1000+i*100);
  receive("CRABIT:USB:STATUS 0123abcd\nCRABIT:USB:ECHO A0b1C2d3\n"); drain(2);
  CHECK(reply_count == 2 && report_count == 0);
  CHECK(!strcmp(replies[0], "CRABIT:USB:STATUS 0123abcd cdc=1 hid=1 test=IDLE\n"));
  CHECK(!strcmp(replies[1], "CRABIT:USB:ECHO A0b1C2d3\n"));
}
static void sequence(void) {
  static const uint8_t expected[16] = {6,21,4,5,12,23,44,24,22,5,44,23,8,22,23,40};
  CHECK(CrabitUsb_KeyboardRequest() == USBD_OK);
  sof(1019); CHECK(report_count == 0);
  for (unsigned i=0; i<32; ++i) {
    uint32_t now = 1020+i*40;
    sof(now); CHECK(report_count == i+1);
    for (unsigned j=0; j<8; ++j) CHECK(reports[i][j] == (j==2 && !(i&1) ? expected[i/2] : 0));
    CHECK(CrabitUsb_KeyboardRequest() == USBD_BUSY);
    sof(now+19); CHECK(report_count == i+1);
    CHECK(CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_RUNNING);
    complete();
    if (i<31) { sof(now+19); CHECK(report_count == i+1); }
  }
  CHECK(CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_RUNNING);
  sof(tick); CHECK(CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_DONE);
  sof(tick+100); CHECK(report_count == 32);
  for (unsigned i=1; i<32; ++i) CHECK(report_times[i]-report_times[i-1] >= 20);
}
static void send_busy_case(void) {
  send_busy=1; CHECK(CrabitUsb_KeyboardRequest() == USBD_OK);
  sof(1020); CHECK(report_count == 0);
  sof(1021); CHECK(report_count == 1 && reports[0][2] == 6);
  complete(); sof(1040); CHECK(report_count == 1);
  sof(1041); CHECK(report_count == 2 && reports[1][2] == 0);
}
static void cancel(uint8_t flash, uint8_t wrap) {
  if (wrap) tick = UINT32_MAX-1000;
  uint32_t start = tick;
  CHECK(CrabitUsb_KeyboardRequest() == USBD_OK);
  sof(start+20); CHECK(report_count == 1 && reports[0][2] == 6);
  bw16_flash_mode=flash;
  sof(start+(flash ? 21 : 4999));
  if (!flash) CHECK(CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_RUNNING);
  sof(start+(flash ? 22 : 5000));
  CHECK(CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_FAILED);
  CHECK(report_count == 1); /* No UI tick, busy report must stay intact. */
  complete(); sof(tick+1); CHECK(report_count == 2);
  for (unsigned j=0; j<8; ++j) CHECK(reports[1][j] == 0);
  CHECK(CrabitUsb_KeyboardRequest() != USBD_OK);
  complete(); sof(tick+100); CHECK(report_count == 2);
}
static void reset_reinit(void) {
  CHECK(CrabitUsb_KeyboardRequest() == USBD_OK); sof(1020);
  CHECK(CrabitUsb_KeyboardInterface.DeInit() == USBD_OK);
  /* USB reset terminates LL ownership before Init can clear its static buffer. */
  pending=NULL; hid.state=CUSTOM_HID_IDLE;
  CHECK(CrabitUsb_KeyboardInterface.Init() == USBD_OK);
  for (unsigned i=0;i<100;++i) sof(2000+i*100);
  CHECK(report_count == 1 && CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_IDLE);
}
static void led_rearm(void) {
  CHECK(CrabitUsb_KeyboardInterface.OutEvent(0, 2) == USBD_OK);
  CHECK(CrabitUsb_KeyboardInterface.OutEvent(0, 0) == USBD_OK);
  CHECK(rearmed == 2 && report_count == 0);
}
static void diag_fragments(void) {
  const char *s="CRABIT:USB:STATUS 1234AbCd\n";
  for (size_t i=0;i<strlen(s);++i) {
    CrabitUsb_DiagReceive((const uint8_t *)s+i, 1); CrabitUsb_DiagTick();
    CHECK(reply_count == (i==strlen(s)-1 ? 1U : 0U));
  }
  CHECK(!strcmp(replies[0],"CRABIT:USB:STATUS 1234AbCd cdc=1 hid=1 test=IDLE\n"));
  receive("CRABIT:USB:ECHO a1b2c3d4\r\n"); drain(1);
  CHECK(!strcmp(replies[1],"CRABIT:USB:ECHO a1b2c3d4\n"));
  CHECK(report_count == 0);
}
static void diag_malformed(void) {
  const char *bad[]={"CRABIT:USB:TYPE 123\n", "CRABIT:USB:TYPE 1234567z\n",
    "CRABIT:USB:TYPE 123456789\n", "CRABIT:USB:TYPE 12345678 extra\n",
    "CRABIT:USB:TYPE  12345678\n", "TYPE 12345678\n", "CRABIT:USB:BOGUS 12345678\n"};
  for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { receive(bad[i]); drain(1); }
  CHECK(reply_count == 0 && CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_IDLE);
  uint8_t bytes[]={ 'C', 0, 'x', '\n', 0x80, '\n' };
  CrabitUsb_DiagReceive(bytes,sizeof(bytes)); drain(1);
  CHECK(reply_count == 1 && !strcmp(replies[0],"CRABIT:USB:ERROR OVERFLOW\n"));
  char oversized[100]; memset(oversized,'A',sizeof(oversized)); oversized[99]='\n';
  CrabitUsb_DiagReceive((uint8_t *)oversized,sizeof(oversized)); drain(1);
  CHECK(reply_count == 2);
  receive("CRABIT:USB:ECHO 12345678\n"); drain(1);
  CHECK(reply_count == 3 && !strcmp(replies[2],"CRABIT:USB:ECHO 12345678\n"));
  sof(2000); CHECK(report_count == 0);
}
static void diag_overflow(void) {
  for (unsigned i=0;i<4;++i) receive("CRABIT:USB:ECHO 12345678\n");
  receive("CRABIT:USB:TYPE deadbeef\n"); drain(8);
  CHECK(reply_count == 5 && !strcmp(replies[0],"CRABIT:USB:ERROR OVERFLOW\n"));
  for (unsigned i=1;i<5;++i) CHECK(!strcmp(replies[i],"CRABIT:USB:ECHO 12345678\n"));
  CHECK(CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_IDLE && report_count == 0);
}
static void diag_type_busy(void) {
  receive("CRABIT:USB:TYPE 01234567\n"); drain(1);
  CHECK(!strcmp(replies[0],"CRABIT:USB:TYPE 01234567 QUEUED\n"));
  CHECK(report_count == 0); sof(1020);
  receive("CRABIT:USB:TYPE 89abcdef\n"); drain(1);
  CHECK(!strcmp(replies[1],"CRABIT:USB:TYPE 89abcdef BUSY\n"));
  complete(); sof(1040); CHECK(report_count == 2 && reports[1][2] == 0);
}
static void diag_blocked(uint8_t result) {
  cdc_result=result;
  receive("CRABIT:USB:ECHO 12345678\nCRABIT:USB:TYPE abcdef12\n");
  drain(1); CHECK(cdc_calls == 1 && reply_count == 0);
  tick=2999; drain(1); CHECK(cdc_calls == 2 && CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_IDLE);
  tick=3000; drain(1); CHECK(cdc_calls == 3);
  cdc_result=USBD_OK; drain(1);
  CHECK(reply_count == 1 && !strcmp(replies[0],"CRABIT:USB:TYPE abcdef12 QUEUED\n"));
  CHECK(report_count == 0 && primask == 0);
}
static void diag_reset(void) {
  cdc_result=USBD_BUSY;
  receive("CRABIT:USB:ECHO 12345678\nCRABIT:USB:TYPE abcdef12\n"); drain(1);
  CrabitUsb_DiagReset(); cdc_result=USBD_OK; drain(4);
  receive("CRABIT:USB:TYPE 1234"); CrabitUsb_DiagReset(); receive("5678\n"); drain(2);
  CHECK(reply_count == 0 && CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_IDLE);
  sof(2000); CHECK(report_count == 0);
}
static void diag_reset_race(void) {
  receive("CRABIT:USB:TYPE 12345678\n");
  reset_on_restore=irq_restores+1; drain(1); reset_on_restore=0;
  CHECK(CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_IDLE && reply_count == 0);
}
static void diag_flash(void) {
  receive("CRABIT:USB:TYPE 12345678\n"); bw16_flash_mode=1; drain(1);
  receive("CRABIT:USB:TYPE abcdef12\n"); bw16_flash_mode=0; drain(4);
  CHECK(report_count == 0 && reply_count == 0 && CrabitUsb_KeyboardStatus() == CRABIT_KEYBOARD_IDLE);
}
static void flash_queue_sof(void) {
  receive("CRABIT:USB:TYPE 12345678\nCRABIT:USB:TYPE 89ab");
  bw16_flash_mode=1; sof(1100); /* UI task never runs in flash mode. */
  bw16_flash_mode=0; receive("cdef\n"); drain(5);
  CHECK(reply_count==0 && CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_IDLE);
  cdc_result=USBD_BUSY;
  receive("CRABIT:USB:ECHO 12345678\nCRABIT:USB:TYPE abcdef12\n"); drain(1);
  bw16_flash_mode=1; sof(1200); bw16_flash_mode=0; cdc_result=USBD_OK; drain(5);
  CHECK(reply_count==0 && CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_IDLE && report_count==0);
}
static void suspend_case(void) {
  CHECK(CrabitUsb_KeyboardRequest()==USBD_OK); sof(1020); CHECK(reports[0][2]==6);
  CrabitUsb_DiagReset(); CrabitUsb_KeyboardSuspend();
  CHECK(CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_FAILED);
  CHECK(memcmp(pending,pending_copy,8)==0); complete();
  sof(1040); CHECK(report_count==2);
  for(unsigned j=0;j<8;++j) CHECK(reports[1][j]==0);
  complete(); sof(1060); CHECK(report_count==2 && CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_FAILED);
}
static void get_report(void) {
  CHECK(CrabitUsb_KeyboardRequest()==USBD_OK); sof(1020);
  uint16_t n=0; hUsbDeviceFS.request.wValue=0x0100;
  uint8_t *snapshot=CrabitUsb_KeyboardInterface.GetReport(&n);
  CHECK(snapshot && snapshot!=pending && n==8 && !memcmp(snapshot,pending,8));
  complete(); sof(1040); CHECK(pending_copy[2]==0 && snapshot[2]==6);
  hUsbDeviceFS.request.wValue=0x0200;
  CHECK(CrabitUsb_KeyboardInterface.OutEvent(0x1f,0)==USBD_OK);
  uint8_t *led=CrabitUsb_KeyboardInterface.GetReport(&n);
  CHECK(led && n==1 && *led==0x1f && led!=pending && led!=snapshot);
  hUsbDeviceFS.request.wValue=0x0300;
  CHECK(!CrabitUsb_KeyboardInterface.GetReport(&n) && n==0);
  hUsbDeviceFS.request.wValue=0x0101;
  CHECK(!CrabitUsb_KeyboardInterface.GetReport(&n) && n==0);
  CHECK(memcmp(pending,pending_copy,8)==0);
}
static void script_sequence(void) {
  const CrabitUsbKeyAction actions[] = {{0,2,4}, {100,0,0}, {0,0,40}};
  CHECK(CrabitUsb_KeyboardAvailable() && !CrabitUsb_KeyboardBusy());
  CHECK(CrabitUsb_KeyboardRun(actions,3,3000)==USBD_OK);
  sof(1019); CHECK(report_count==0);
  sof(1020); CHECK(report_count==1 && reports[0][0]==2 && reports[0][2]==4); complete();
  sof(1040); CHECK(report_count==2 && reports[1][0]==0 && reports[1][2]==0); complete();
  sof(1139); CHECK(report_count==2);
  sof(1140); CHECK(report_count==2);
  sof(1160); CHECK(report_count==3 && reports[2][2]==40); complete();
  sof(1180); CHECK(report_count==4 && reports[3][2]==0);
  CHECK(CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_RUNNING);
  complete(); sof(1181);
  CHECK(CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_DONE && !CrabitUsb_KeyboardBusy());
}
static void script_cancel(void) {
  const CrabitUsbKeyAction actions[] = {{0,2,4},{0,0,5}};
  CHECK(CrabitUsb_KeyboardRun(actions,2,3000)==USBD_OK);
  sof(1020); CHECK(report_count==1);
  CrabitUsb_KeyboardCancel();
  CHECK(CrabitUsb_KeyboardBusy() && CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_FAILED);
  sof(1040); CHECK(report_count==1); complete();
  sof(1041); CHECK(report_count==2);
  for(unsigned i=0;i<8;++i) CHECK(reports[1][i]==0);
  CHECK(CrabitUsb_KeyboardBusy()); complete();
  CHECK(!CrabitUsb_KeyboardBusy()); sof(1200); CHECK(report_count==2);
}
static void script_limits(void) {
  const CrabitUsbKeyAction action={0,0,4};
  CHECK(CrabitUsb_KeyboardRun(NULL,1,1000)==USBD_FAIL);
  CHECK(CrabitUsb_KeyboardRun(&action,129,1000)==USBD_FAIL);
  CHECK(CrabitUsb_KeyboardRun(&action,1,32001)==USBD_FAIL);
  bw16_flash_mode=1;
  CHECK(!CrabitUsb_KeyboardAvailable() && CrabitUsb_KeyboardRun(&action,1,1000)==USBD_FAIL);
  bw16_flash_mode=0; hUsbDeviceFS.dev_state=USBD_STATE_DEFAULT;
  CHECK(!CrabitUsb_KeyboardAvailable() && CrabitUsb_KeyboardRun(&action,1,1000)==USBD_FAIL);
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;
  CHECK(CrabitUsb_KeyboardRequest()==USBD_OK);
  CHECK(CrabitUsb_KeyboardRun(&action,1,1000)==USBD_BUSY);
}
static void allocator(void) {
  for (unsigned cycle=0;cycle<20;++cycle) {
    void *c=CrabitUsb_Allocate(sizeof(USBD_CDC_HandleTypeDef));
    void *h=CrabitUsb_Allocate(sizeof(USBD_CUSTOM_HID_HandleTypeDef));
    CHECK(c && h && c!=h);
    CHECK((uintptr_t)c % _Alignof(USBD_CDC_HandleTypeDef) == 0);
    CHECK((uintptr_t)h % _Alignof(USBD_CUSTOM_HID_HandleTypeDef) == 0);
    CHECK(!CrabitUsb_Allocate(sizeof(USBD_CDC_HandleTypeDef)));
    CHECK(!CrabitUsb_Allocate(sizeof(USBD_CUSTOM_HID_HandleTypeDef)));
    CHECK(!CrabitUsb_Allocate(1));
    memset(c,0xa5,sizeof(USBD_CDC_HandleTypeDef));
    memset(h,0x5a,sizeof(USBD_CUSTOM_HID_HandleTypeDef));
    CrabitUsb_Free(c); CrabitUsb_Free(c); CrabitUsb_Free(NULL);
    uint8_t *again=CrabitUsb_Allocate(sizeof(USBD_CDC_HandleTypeDef)); CHECK(again==c);
    for (size_t i=0;i<sizeof(USBD_CDC_HandleTypeDef);++i) CHECK(again[i]==0);
    CHECK(((uint8_t *)h)[0]==0x5a); CrabitUsb_Free(h); CrabitUsb_Free(again);
  }
}
static void registration(void) {
  CHECK(CrabitUsb_Register(NULL)==USBD_FAIL);
  for (unsigned failure=0;failure<=4;++failure) {
    USBD_HandleTypeDef d={0}; register_calls=0; register_fail=failure;
    CHECK(CrabitUsb_Register(&d)==(failure ? USBD_FAIL : USBD_OK)); CHECK(d.classId==0);
    if (!failure) { CHECK(d.NumClasses==2 && d.pClass[1]->SOF!=NULL); CHECK(CrabitUsb_Register(&d)==USBD_FAIL); }
  }
}
int main(int argc, char **argv) {
  CHECK(argc==2); setup(); const char *name=argv[1];
  if (!strcmp(name,"idle")) idle();
  else if (!strcmp(name,"sequence")) sequence();
  else if (!strcmp(name,"send_busy")) send_busy_case();
  else if (!strcmp(name,"deadline")) cancel(0,0);
  else if (!strcmp(name,"tick_wrap")) cancel(0,1);
  else if (!strcmp(name,"flash_sof")) cancel(1,0);
  else if (!strcmp(name,"reset_reinit")) reset_reinit();
  else if (!strcmp(name,"led_rearm")) led_rearm();
  else if (!strcmp(name,"diag_fragments")) diag_fragments();
  else if (!strcmp(name,"diag_malformed")) diag_malformed();
  else if (!strcmp(name,"diag_overflow")) diag_overflow();
  else if (!strcmp(name,"diag_type_busy")) diag_type_busy();
  else if (!strcmp(name,"diag_cdc_busy")) diag_blocked(USBD_BUSY);
  else if (!strcmp(name,"diag_cdc_fail")) diag_blocked(USBD_FAIL);
  else if (!strcmp(name,"diag_reset")) diag_reset();
  else if (!strcmp(name,"diag_reset_race")) diag_reset_race();
  else if (!strcmp(name,"diag_flash")) diag_flash();
  else if (!strcmp(name,"flash_queue_sof")) flash_queue_sof();
  else if (!strcmp(name,"suspend")) suspend_case();
  else if (!strcmp(name,"get_report")) get_report();
  else if (!strcmp(name,"script_sequence")) script_sequence();
  else if (!strcmp(name,"script_cancel")) script_cancel();
  else if (!strcmp(name,"script_limits")) script_limits();
  else if (!strcmp(name,"allocator")) allocator();
  else if (!strcmp(name,"registration")) registration();
  else CHECK(0);
  printf("PASS %s\n",name); return 0;
}
