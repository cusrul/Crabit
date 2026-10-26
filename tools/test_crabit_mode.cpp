#include <assert.h>
#include <string.h>
#include "../firmware/shared/crabit_mode.h"
#include "../firmware/shared/crabit_wire.h"

int main() {
    using namespace CrabitMode;
    Coordinator coordinator;
    assert(coordinator.lease().phase == Phase::IDLE);
    assert(coordinator.request(Mode::BW16_EXPERIENCE, Owner::BW16,
                               RESOURCE_DISPLAY | RESOURCE_HIGH_PERFORMANCE,
                               100, 1000));
    assert(coordinator.lease().phase == Phase::PREPARING);
    assert(coordinator.grant(200));
    assert(coordinator.activate(210));
    assert(coordinator.heartbeat(500));
    assert(!coordinator.tick(900, 500));
    assert(coordinator.tick(1100, 500));
    assert(coordinator.lease().phase == Phase::FAULT);
    coordinator.released();
    assert(coordinator.lease().phase == Phase::IDLE);

    uint8_t payload[CrabitWire::PAYLOAD_SIZE];
    uint8_t frame[CrabitWire::FRAME_SIZE];
    size_t payload_size = 0;
    size_t frame_size = 0;
    assert(CrabitWire::encode_message("sys/time", "2026-07-31", 4321,
                                      payload, sizeof(payload), &payload_size));
    assert(CrabitWire::encode_frame(payload, payload_size, frame, sizeof(frame), &frame_size));
    uint8_t decoded_payload[CrabitWire::PAYLOAD_SIZE];
    size_t decoded_size = 0;
    CrabitWire::Message message{};
    assert(CrabitWire::decode_frame(frame, frame_size, decoded_payload,
                                    sizeof(decoded_payload), &decoded_size));
    assert(CrabitWire::decode_message(decoded_payload, decoded_size, &message));
    assert(message.request_id == 4321);
    assert(strcmp(message.topic, "sys/time") == 0);
    return 0;
}
