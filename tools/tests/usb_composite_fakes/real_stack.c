/* Unmodified ST Core + Builder + CDC + CustomHID and actual product sources.
 * Only the MCU HAL and USB LL are fake. No state-machine or descriptor copies. */
#include <stdio.h>
#include <stdlib.h>
#include "usb_composite.h"
#include "usb_composite_config.h"
#include "usb_keyboard.h"
#include "usb_diag.h"
#include "usbd_composite_builder.h"
#include "usbd_cdc_if.h"
#include "usart.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
USBD_HandleTypeDef hUsbDeviceFS;
volatile uint8_t bw16_flash_mode;
UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;
volatile uint16_t ring_last_head;
uint8_t uart_rx_ring_buf[2048];
extern volatile uint8_t usb_rx_ready;
extern volatile uint32_t usb_rx_len;
extern uint8_t UserTxBufferFS[];
void CDC_Resume_Receive(void);
static PCD_HandleTypeDef pcd;
static uint32_t tick=1000, mask;
static unsigned hal_uart_calls;
typedef struct {
  uint8_t *pointer;
  uint8_t snapshot[2048];
  uint32_t size;
  unsigned sends, arms;
  uint8_t opened, stalled;
  uint16_t mps;
} Endpoint;
static Endpoint in[16], out[16];
static uint32_t rx_size[16];

uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t delay) { tick+=delay; }
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask=1; }
void __set_PRIMASK(uint32_t value) { mask=value; }
int HAL_UART_DeInit(UART_HandleTypeDef *p) { (void)p; ++hal_uart_calls; return 0; }
int HAL_UART_Init(UART_HandleTypeDef *p) { (void)p; ++hal_uart_calls; return 0; }
int HAL_DMA_Init(DMA_HandleTypeDef *p) { (void)p; ++hal_uart_calls; return 0; }
int HAL_UART_Receive_DMA(UART_HandleTypeDef *p, uint8_t *b, uint16_t n) {
  (void)p; (void)b; (void)n; ++hal_uart_calls; return 0;
}
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *d) { d->pData=&pcd; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *d) { (void)d; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *d) { (void)d; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *d) { (void)d; return USBD_OK; }
static Endpoint *ep(uint8_t address) { return address&0x80 ? &in[address&15] : &out[address&15]; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *d, uint8_t a, uint8_t type, uint16_t size) {
  (void)type; Endpoint *e=ep(a); e->opened=1; e->mps=size;
  if(a&0x80) { pcd.IN_ep[a&15].maxpacket=size; d->ep_in[a&15].maxpacket=size; }
  else d->ep_out[a&15].maxpacket=size;
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *d, uint8_t a) {
  (void)d; ep(a)->opened=0; ep(a)->pointer=NULL; return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *d, uint8_t a) {
  (void)d; ep(a)->pointer=NULL; return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *d, uint8_t a) {
  (void)d; ep(a)->stalled=1; return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *d, uint8_t a) {
  (void)d; ep(a)->stalled=0; return USBD_OK;
}
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *d, uint8_t a) { (void)d; return ep(a)->stalled; }
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *d, uint8_t a) {
  (void)d; (void)a; return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *d, uint8_t a, uint8_t *p, uint32_t n) {
  (void)d; Endpoint *e=&in[a&15]; CHECK(n<=sizeof(e->snapshot));
  if (a&15) CHECK(e->pointer==NULL); /* LL retains pointer until DataIn. */
  e->pointer=p; e->size=n; ++e->sends;
  if(n) { CHECK(p!=NULL); memcpy(e->snapshot,p,n); }
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *d, uint8_t a, uint8_t *p, uint32_t n) {
  (void)d; Endpoint *e=&out[a&15]; e->pointer=p; e->size=n; ++e->arms; return USBD_OK;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *d, uint8_t a) { (void)d; return rx_size[a&15]; }
void USBD_LL_Delay(uint32_t delay) { HAL_Delay(delay); }

static void complete(uint8_t number) {
  Endpoint *e=&in[number]; CHECK(e->pointer!=NULL || e->size==0);
  if(e->size) CHECK(memcmp(e->pointer,e->snapshot,e->size)==0);
  e->pointer=NULL;
  CHECK(USBD_LL_DataInStage(&hUsbDeviceFS,number,NULL)==USBD_OK);
}
static void sof(uint32_t now) {
  tick=now; CHECK(USBD_LL_SOF(&hUsbDeviceFS)==USBD_OK);
  for(unsigned i=1;i<16;++i) if(in[i].pointer) CHECK(memcmp(in[i].pointer,in[i].snapshot,in[i].size)==0);
}
static void host_out(uint8_t number,const uint8_t *p,size_t n) {
  Endpoint *e=&out[number]; CHECK(e->pointer && n<=e->size);
  uint8_t *buffer=e->pointer; memcpy(buffer,p,n); e->pointer=NULL; rx_size[number]=(uint32_t)n;
  CHECK(USBD_LL_DataOutStage(&hUsbDeviceFS,number,buffer)==USBD_OK);
}
static void command(const char *s) { host_out(1,(const uint8_t *)s,strlen(s)); CrabitUsb_DiagTick(); }
static void setup_packet(uint8_t bm,uint8_t request,uint16_t value,uint16_t index,uint16_t length) {
  uint8_t data[8]={bm,request,(uint8_t)value,(uint8_t)(value>>8),(uint8_t)index,
                   (uint8_t)(index>>8),(uint8_t)length,(uint8_t)(length>>8)};
  CHECK(USBD_LL_SetupStage(&hUsbDeviceFS,data)==USBD_OK);
}
static void address_guard(void) {
  uint16_t length=0;
  uint8_t *p=USBD_CMPSIT.GetFSConfigDescriptor(&length);
  CHECK(p && (uintptr_t)p <= UINT32_MAX-USBD_CMPST_MAX_CONFDESC_SZ);
  printf("builder buffer address safe: %llu\n",(unsigned long long)(uintptr_t)p);
}
static void configure(void) {
  hUsbDeviceFS.dev_state=USBD_STATE_ADDRESSED;
  setup_packet(0,USB_REQ_SET_CONFIGURATION,1,0,0);
  CHECK(hUsbDeviceFS.dev_state==USBD_STATE_CONFIGURED);
  CHECK(hUsbDeviceFS.pClassDataCmsit[0] && hUsbDeviceFS.pClassDataCmsit[1]);
  CHECK(hUsbDeviceFS.pClassDataCmsit[0]!=hUsbDeviceFS.pClassDataCmsit[1]);
}
static void start(void) {
  address_guard();
  CHECK(USBD_Init(&hUsbDeviceFS,NULL,0)==USBD_OK);
  CHECK(USBD_LL_SetSpeed(&hUsbDeviceFS,USBD_SPEED_FULL)==USBD_OK);
  CHECK(CrabitUsb_Register(&hUsbDeviceFS)==USBD_OK);
  CHECK(USBD_MAX_SUPPORTED_CLASS==CRABIT_USB_CLASS_COUNT && hUsbDeviceFS.NumClasses==2);
  CHECK(USBD_LL_Reset(&hUsbDeviceFS)==USBD_OK);
  configure();
}
static void descriptor(void) {
  uint16_t n=0; uint8_t *d=USBD_CMPSIT.GetFSConfigDescriptor(&n);
  CHECK(n==107 && d[0]==9 && d[1]==2 && d[2]==107 && d[3]==0 && d[4]==3);
  unsigned ifs=0,iad=0,eps=0,hid=0,addresses=0,current=99;
  for(unsigned off=0;off<n;) {
    uint8_t len=d[off],type=d[off+1]; CHECK(len>=2 && off+len<=n);
    if(type==USB_DESC_TYPE_INTERFACE) {
      CHECK(len==9 && d[off+2]==ifs); current=ifs++;
      CHECK(d[off+5]==(current==0 ? 2 : current==1 ? 10 : 3));
    } else if(type==USB_DESC_TYPE_IAD) {
      ++iad; CHECK(len==8 && d[off+2]==0 && d[off+3]==2 && d[off+4]==2);
    } else if(type==USB_DESC_TYPE_ENDPOINT) {
      ++eps; uint8_t a=d[off+2];
      if(a==0x81) { CHECK(current==1); addresses|=1; }
      else if(a==0x01) { CHECK(current==1); addresses|=2; }
      else if(a==0x82) { CHECK(current==0); addresses|=4; }
      else if(a==0x83) { CHECK(current==2 && d[off+4]==8); addresses|=8; }
      else if(a==0x03) { CHECK(current==2 && d[off+4]==2); addresses|=16; }
      else CHECK(0);
    } else if(type==0x21) { ++hid; CHECK(current==2 && d[off+7]==63 && d[off+8]==0); }
    off+=len;
  }
  CHECK(ifs==3 && iad==1 && eps==5 && addresses==31 && hid==1);
  CHECK(USBD_CoreFindIF(&hUsbDeviceFS,0)==0 && USBD_CoreFindIF(&hUsbDeviceFS,1)==0);
  CHECK(USBD_CoreFindIF(&hUsbDeviceFS,2)==1);
  CHECK(USBD_CoreFindEP(&hUsbDeviceFS,0x81)==0 && USBD_CoreFindEP(&hUsbDeviceFS,0x01)==0);
  CHECK(USBD_CoreFindEP(&hUsbDeviceFS,0x82)==0 && USBD_CoreFindEP(&hUsbDeviceFS,0x83)==1);
  CHECK(USBD_CoreFindEP(&hUsbDeviceFS,0x03)==1);
  setup_packet(0x81,USB_REQ_GET_DESCRIPTOR,0x2200,2,63);
  CHECK(hUsbDeviceFS.classId==1 && in[0].size==63);
  CHECK(!memcmp(in[0].snapshot,CrabitUsb_KeyboardInterface.pReport,63));
}
static void routing(void) {
  sof(1000); CHECK(hUsbDeviceFS.classId==1 && in[3].sends==0);
  uint8_t text[]="CDC works";
  CHECK(CDC_Transmit_FS(text,sizeof(text)-1)==USBD_OK);
  CHECK(hUsbDeviceFS.classId==1 && in[1].sends==1 && in[3].sends==0);
  CHECK(in[1].pointer==UserTxBufferFS);
  memset(text,'X',sizeof(text)); CHECK(!memcmp(in[1].pointer,"CDC works",9));
  CHECK(CDC_Transmit_FS(text,9)==USBD_BUSY); CHECK(!memcmp(in[1].pointer,"CDC works",9));
  complete(1); CHECK(hUsbDeviceFS.classId==0);
  command("CRABIT:USB:STATUS a1b2c3d4\n");
  CHECK(in[1].size==strlen("CRABIT:USB:STATUS a1b2c3d4 cdc=1 hid=1 test=IDLE\n"));
  CHECK(!memcmp(in[1].pointer,"CRABIT:USB:STATUS a1b2c3d4 cdc=1 hid=1 test=IDLE\n",in[1].size));
  complete(1); CHECK(in[3].sends==0 && hal_uart_calls==0);
  setup_packet(0xA1,CDC_GET_LINE_CODING,0,0,7);
  CHECK(hUsbDeviceFS.classId==0 && in[0].size==7);
}
static void keyboard_completion(void) {
  static const uint8_t keys[16]={6,21,4,5,12,23,44,24,22,5,44,23,8,22,23,40};
  command("CRABIT:USB:TYPE 12345678\n"); CHECK(in[3].sends==0);
  CHECK(!memcmp(in[1].pointer,"CRABIT:USB:TYPE 12345678 QUEUED\n",in[1].size)); complete(1);
  for(unsigned i=0;i<32;++i) {
    uint32_t now=1020+i*40; sof(now);
    CHECK(in[3].sends==i+1 && in[3].size==8);
    for(unsigned j=0;j<8;++j) CHECK(in[3].snapshot[j]==(j==2 && !(i&1) ? keys[i/2] : 0));
    CHECK(CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_RUNNING);
    sof(now+19); CHECK(in[3].sends==i+1);
    complete(3); CHECK(hUsbDeviceFS.classId==1);
  }
  CHECK(CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_RUNNING);
  sof(tick); CHECK(CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_DONE);
}
static void led_out(void) {
  unsigned before=out[3].arms; uint8_t leds[2]={0,2};
  host_out(3,leds,2); CHECK(hUsbDeviceFS.classId==1 && out[3].arms==before+1);
  CHECK(in[3].sends==0);
  setup_packet(0x21,CUSTOM_HID_REQ_SET_REPORT,0x0200,2,1);
  CHECK(hUsbDeviceFS.classId==1 && out[0].size==1);
  host_out(0,leds+1,1); CHECK(out[3].arms==before+2 && in[3].sends==0);
}
static void flash_rx(void) {
  unsigned before=out[1].arms;
  bw16_flash_mode=1;
  const char *type="CRABIT:USB:TYPE 12345678\n";
  host_out(1,(const uint8_t *)type,strlen(type));
  CHECK(usb_rx_ready==1 && usb_rx_len==strlen(type));
  CHECK(out[1].arms==before && out[1].pointer==NULL);
  CrabitUsb_DiagTick(); sof(1100); CHECK(in[3].sends==0 && in[1].sends==0);
  hUsbDeviceFS.classId=1; CDC_Resume_Receive(); CHECK(out[1].arms==before+1);
  CHECK(hUsbDeviceFS.classId==1); bw16_flash_mode=0;
  command("CRABIT:USB:ECHO 11223344\n"); CHECK(out[1].arms==before+2);
  CHECK(in[1].sends==1 && in[3].sends==0);
}
static void reset_cycles(void) {
  void *cdc=hUsbDeviceFS.pClassDataCmsit[0], *hid=hUsbDeviceFS.pClassDataCmsit[1];
  for(unsigned i=0;i<12;++i) {
    command("CRABIT:USB:TYPE 12345678\n"); sof(tick+20);
    CHECK(in[3].pointer!=NULL);
    CHECK(USBD_LL_Reset(&hUsbDeviceFS)==USBD_OK);
    CHECK(!hUsbDeviceFS.pClassDataCmsit[0] && !hUsbDeviceFS.pClassDataCmsit[1]);
    configure();
    CHECK(hUsbDeviceFS.pClassDataCmsit[0]==cdc && hUsbDeviceFS.pClassDataCmsit[1]==hid);
    unsigned sent=in[3].sends; sof(tick+100); CrabitUsb_DiagTick();
    CHECK(in[3].sends==sent && CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_IDLE);
  }
}
static void allocation_failure(void) {
  CHECK(USBD_LL_Reset(&hUsbDeviceFS)==USBD_OK);
  void *reserved=CrabitUsb_Allocate(sizeof(USBD_CUSTOM_HID_HandleTypeDef)); CHECK(reserved);
  CHECK(USBD_SetClassConfig(&hUsbDeviceFS,1)==USBD_FAIL);
  CHECK(hUsbDeviceFS.pClassDataCmsit[0] && !hUsbDeviceFS.pClassDataCmsit[1]);
  CHECK(USBD_ClrClassConfig(&hUsbDeviceFS,1)==USBD_OK);
  CrabitUsb_Free(reserved); configure();
  CHECK(USBD_LL_Reset(&hUsbDeviceFS)==USBD_OK);
  reserved=CrabitUsb_Allocate(sizeof(USBD_CDC_HandleTypeDef)); CHECK(reserved);
  CHECK(USBD_SetClassConfig(&hUsbDeviceFS,1)==USBD_FAIL);
  CHECK(!hUsbDeviceFS.pClassDataCmsit[0] && hUsbDeviceFS.pClassDataCmsit[1]);
  CHECK(USBD_ClrClassConfig(&hUsbDeviceFS,1)==USBD_OK);
  CrabitUsb_Free(reserved); configure();
}
static void flash_queue_sof(void) {
  const char *s="CRABIT:USB:TYPE 12345678\nCRABIT:USB:TYPE 89ab";
  host_out(1,(const uint8_t *)s,strlen(s));
  bw16_flash_mode=1; sof(1100); bw16_flash_mode=0;
  host_out(1,(const uint8_t *)"cdef\n",5); CrabitUsb_DiagTick();
  CHECK(CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_IDLE && in[1].sends==0 && in[3].sends==0);
  command("CRABIT:USB:ECHO 12345678\n"); CHECK(in[1].sends==1);
  command("CRABIT:USB:ECHO abcdef12\n"); /* reply blocked by actual CDC TxState */
  s="CRABIT:USB:TYPE 12345678\nCRABIT:USB:TYPE 1122";
  host_out(1,(const uint8_t *)s,strlen(s));
  /* No normal SOF since the first flash exit: repeated boundaries still cancel. */
  bw16_flash_mode=1; sof(1200); bw16_flash_mode=0; complete(1);
  host_out(1,(const uint8_t *)"3344\n",5);
  for(unsigned i=0;i<6;++i) CrabitUsb_DiagTick();
  CHECK(in[1].sends==1 && in[3].sends==0 && CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_IDLE);
}
static void suspend_case(void) {
  command("CRABIT:USB:TYPE 12345678\n"); complete(1); sof(1020);
  CHECK(in[3].snapshot[2]==6);
  const char *queued="CRABIT:USB:TYPE abcdef12\n";
  host_out(1,(const uint8_t *)queued,strlen(queued));
  /* Same target callback order; source guard checks this against usbd_conf.c. */
  CrabitUsb_DiagReset(); CrabitUsb_KeyboardSuspend();
  CHECK(USBD_LL_Suspend(&hUsbDeviceFS)==USBD_OK);
  CHECK(CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_FAILED);
  CHECK(USBD_LL_Resume(&hUsbDeviceFS)==USBD_OK); sof(1040);
  CHECK(in[3].sends==1 && in[3].snapshot[2]==6);
  complete(3); sof(1041); CHECK(in[3].sends==2);
  for(unsigned j=0;j<8;++j) CHECK(in[3].snapshot[j]==0);
  complete(3); sof(1060); CrabitUsb_DiagTick();
  CHECK(in[3].sends==2 && in[1].sends==1 && CrabitUsb_KeyboardStatus()==CRABIT_KEYBOARD_FAILED);
}
static void get_report(void) {
  command("CRABIT:USB:TYPE 12345678\n"); complete(1); sof(1020);
  setup_packet(0xa1,CUSTOM_HID_REQ_GET_REPORT,0x0100,2,8);
  CHECK(hUsbDeviceFS.classId==1 && in[0].size==8);
  CHECK(in[0].pointer!=in[3].pointer && in[0].snapshot[2]==6);
  uint8_t *control=in[0].pointer;
  complete(3); sof(1040);
  CHECK(in[3].snapshot[2]==0 && control[2]==6); complete(0);
  uint8_t leds[2]={0x1f,0}; host_out(3,leds,2);
  setup_packet(0xa1,CUSTOM_HID_REQ_GET_REPORT,0x0200,2,1);
  CHECK(in[0].size==1 && in[0].pointer!=in[3].pointer && in[0].snapshot[0]==0x1f);
  complete(0);
  unsigned sends=in[0].sends;
  setup_packet(0xa1,CUSTOM_HID_REQ_GET_REPORT,0x0300,2,8);
  CHECK(in[0].sends==sends && in[0].stalled && out[0].stalled);
  /* Direct callback additionally verifies invalid report ID returns NULL/zero. */
  hUsbDeviceFS.request.wValue=0x0101;
  uint16_t n=8; CHECK(!CrabitUsb_KeyboardInterface.GetReport(&n) && n==0);
  CHECK(!memcmp(in[3].pointer,in[3].snapshot,8));
}
int main(int argc,char **argv) {
  CHECK(argc==2); start();
  if(!strcmp(argv[1],"descriptor")) descriptor();
  else if(!strcmp(argv[1],"routing")) routing();
  else if(!strcmp(argv[1],"keyboard_completion")) keyboard_completion();
  else if(!strcmp(argv[1],"led_out")) led_out();
  else if(!strcmp(argv[1],"flash_rx")) flash_rx();
  else if(!strcmp(argv[1],"reset_cycles")) reset_cycles();
  else if(!strcmp(argv[1],"allocation_failure")) allocation_failure();
  else if(!strcmp(argv[1],"flash_queue_sof")) flash_queue_sof();
  else if(!strcmp(argv[1],"suspend")) suspend_case();
  else if(!strcmp(argv[1],"get_report")) get_report();
  else CHECK(0);
  CHECK(mask==0); printf("PASS %s\n",argv[1]); return 0;
}
