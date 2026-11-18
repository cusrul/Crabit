#pragma once

#include <stdbool.h>
#include <stdint.h>

// Single-owner gate for USART1 and the USB CDC raw path.
//
// Normal product traffic (CrabitWire RX/TX, USB diag/console) owns the UART
// only while the mode is NORMAL. The BW16 flash bridge and the UART bypass
// screen must claim an exclusive mode before touching pins, power or DMA.
// A failed recovery leaves the gate FAULT: normal traffic stays closed rather
// than silently crossing owners.
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UART_OWNER_NORMAL = 0,
    UART_OWNER_FLASH = 1,
    UART_OWNER_BYPASS = 2,
    UART_OWNER_FAULT = 3,
} UartOwnerMode;

// Quiesce the normal RX path, serialize/drain an in-flight normal TX and claim
// the UART for FLASH or BYPASS. Returns false without changing the owner when
// another claim is active or the normal side cannot be quiesced safely.
bool UartOwner_Claim(UartOwnerMode mode);

// Restore the fixed product UART configuration and the normal ReceiveToIdle
// DMA RX path, then reopen normal traffic last. On failure the owner becomes
// UART_OWNER_FAULT and normal traffic remains closed.
bool UartOwner_Release(UartOwnerMode mode);

UartOwnerMode UartOwner_Active(void);
bool UartOwner_Normal(void);

// CDC line-coding requests are ISR context (USB control). They only record the
// host value here; the exclusive owner applies a pending value between
// transfers so no ISR mutates an active DMA stream.
bool UartOwner_RequestLineCoding(uint32_t baudrate);
bool UartOwner_TakePendingLineCoding(uint32_t* baudrate);

// Effective baud for CDC_GET_LINE_CODING. The owner updates it after a
// successful apply/restore.
uint32_t UartOwner_LineCodingBaud(void);
void UartOwner_SetLineCodingBaud(uint32_t baudrate);

#ifdef __cplusplus
}
#endif
