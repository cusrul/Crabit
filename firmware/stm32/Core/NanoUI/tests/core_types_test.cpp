#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <NanoUI/core/embedded_vector.h>
#include <NanoUI/core/text.h>
#include <NanoUI/core/callback.h>
#include "../../../../shared/crabit_wire.h"

namespace {
int callbackValue = 0;
void addOne(int value) { callbackValue += value; }

class TestMemoryAdapter : public NanoUI::MemoryAdapter {
public:
    void* alloc(size_t size) override { return std::malloc(size); }
    void free(void* ptr) override { std::free(ptr); }
};
}

int main() {
    TestMemoryAdapter adapter;
    NanoUI::MemoryPool::instance().setAdapter(&adapter);

    NanoUI::StaticVector<int, 2> values;
    assert(values.push_back(1));
    assert(values.push_back(2));
    assert(!values.push_back(3));
    assert(values.overflowedCapacity());
    assert(values.size() == 2);

    NanoUI::StaticVector<int, 2> copied(values);
    assert(copied[0] == 1 && copied[1] == 2);

    NanoUI::EmbeddedVector<int> pooled;
    for (int i = 0; i < 16; ++i) assert(pooled.push_back(i));
    assert(!pooled.push_back(16));
    assert(pooled.overflowedCapacity());
    NanoUI::EmbeddedVector<int> moved(static_cast<NanoUI::EmbeddedVector<int>&&>(pooled));
    assert(moved.size() == 16 && moved.back() == 15);

    NanoUI::StaticString<5> text("hello");
    assert(text.size() == 5);
    assert(!text.append("!"));
    assert(NanoUI::StringView("hello").equals(text.view()));

    NanoUI::Callback<void(int)> callback(addOne);
    callback(7);
    assert(callbackValue == 7);

    uint8_t payload[CrabitWire::PAYLOAD_SIZE];
    uint8_t frame[CrabitWire::FRAME_SIZE];
    size_t payloadSize = 0;
    size_t frameSize = 0;
    assert(CrabitWire::encode_message("sys/mode", "BLE", 7, payload, sizeof(payload), &payloadSize));
    assert(CrabitWire::encode_frame(payload, payloadSize, frame, sizeof(frame), &frameSize));
    CrabitWire::Message message = {};
    uint8_t decoded[CrabitWire::PAYLOAD_SIZE];
    size_t decodedSize = 0;
    assert(CrabitWire::decode_frame(frame, frameSize, decoded, sizeof(decoded), &decodedSize));
    assert(CrabitWire::decode_message(decoded, decodedSize, &message));
    assert(message.request_id == 7);
    assert(std::strcmp(message.topic, "sys/mode") == 0);
    assert(std::strcmp(message.value, "BLE") == 0);
    frame[frameSize - 1] ^= 1;
    assert(!CrabitWire::decode_frame(frame, frameSize, decoded, sizeof(decoded), &decodedSize));
    return 0;
}
