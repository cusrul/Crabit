#pragma once

#include "FreeRTOS.h"
#include "crabit_wire.h"
#include "queue.h"

// Transport queues only. EventBus owns dispatch; LinkQueue owns the two
// fixed-depth queues and wire encoding.
class LinkQueue {
public:
    static bool init();
    static bool ready();
    static bool send(const char* topic, const char* value, uint16_t request_id);
    static bool enqueueUi(const CrabitWire::Message& message);
    static bool receiveTx(CrabitWire::Message& message, TickType_t timeout);
    static bool receiveUi(CrabitWire::Message& message, TickType_t timeout);
    static UBaseType_t txDepth();

private:
    static QueueHandle_t tx_;
    static QueueHandle_t ui_;
};
