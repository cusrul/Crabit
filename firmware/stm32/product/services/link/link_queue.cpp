#include "link_queue.h"

#include "main.h"

QueueHandle_t LinkQueue::tx_ = nullptr;
QueueHandle_t LinkQueue::ui_ = nullptr;

bool LinkQueue::init() {
    tx_ = xQueueCreate(3, sizeof(CrabitWire::Message));
    ui_ = xQueueCreate(3, sizeof(CrabitWire::Message));
    return tx_ != nullptr && ui_ != nullptr;
}

bool LinkQueue::ready() {
    return tx_ != nullptr && ui_ != nullptr;
}

bool LinkQueue::send(const char* topic, const char* value, uint16_t request_id) {
    if (tx_ == nullptr)
        return false;
    CrabitWire::Message message = {};
    uint8_t payload[CrabitWire::PAYLOAD_SIZE];
    size_t payload_len = 0;
    if (!CrabitWire::encode_message(topic, value, request_id, payload, sizeof(payload),
                                    &payload_len))
        return false;
    if (!CrabitWire::decode_message(payload, payload_len, &message))
        return false;

    if (__get_IPSR() != 0) {
        BaseType_t woken = pdFALSE;
        const BaseType_t sent = xQueueSendFromISR(tx_, &message, &woken);
        portYIELD_FROM_ISR(woken);
        return sent == pdPASS;
    }
    return xQueueSend(tx_, &message, pdMS_TO_TICKS(10)) == pdPASS;
}

bool LinkQueue::enqueueUi(const CrabitWire::Message& message) {
    if (ui_ == nullptr)
        return false;
    return xQueueSend(ui_, &message, 0) == pdPASS;
}

bool LinkQueue::receiveTx(CrabitWire::Message& message, TickType_t timeout) {
    return tx_ != nullptr && xQueueReceive(tx_, &message, timeout) == pdPASS;
}

bool LinkQueue::receiveUi(CrabitWire::Message& message, TickType_t timeout) {
    return ui_ != nullptr && xQueueReceive(ui_, &message, timeout) == pdPASS;
}

UBaseType_t LinkQueue::txDepth() {
    return tx_ != nullptr ? uxQueueMessagesWaiting(tx_) : 0;
}
