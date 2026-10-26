#pragma once
#include "crabit_wire.h"

// Capture callbacks enqueue copies; UI owner pumps UART, UART worker posts ACKs.
namespace AuditRelay {
void begin();
bool enqueue(const char* json);
void receive(const CrabitWire::Message& message);
void tick();
unsigned stored();
unsigned dropped();
const char* status();
}  // namespace AuditRelay
