#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <deque>

#include "../firmware/shared/crabit_wire.h"

static void test_round_trip() {
    uint8_t payload[CrabitWire::PAYLOAD_SIZE]{};
    uint8_t frame[CrabitWire::FRAME_SIZE]{};
    size_t payload_len = 0, frame_len = 0;
    assert(CrabitWire::encode_message("sys/mode", "BLE", 42, payload,
                                      sizeof(payload), &payload_len));
    assert(CrabitWire::encode_frame(payload, payload_len, frame, sizeof(frame), &frame_len));

    // Feed the frame in deliberately awkward fragments, as the UART task does.
    std::vector<uint8_t> stream(frame, frame + frame_len);
    for (size_t split : {size_t(1), size_t(3), frame_len}) {
        assert(split <= stream.size());
        uint8_t decoded[CrabitWire::PAYLOAD_SIZE]{};
        size_t decoded_len = 0;
        CrabitWire::Message message{};
        if (split == frame_len) {
            assert(CrabitWire::decode_frame(stream.data(), split, decoded,
                                            sizeof(decoded), &decoded_len));
            assert(CrabitWire::decode_message(decoded, decoded_len, &message));
            assert(message.request_id == 42);
            assert(strcmp(message.topic, "sys/mode") == 0);
            assert(strcmp(message.value, "BLE") == 0);
        } else {
            assert(!CrabitWire::decode_frame(stream.data(), split, decoded,
                                             sizeof(decoded), &decoded_len));
        }
    }
}

static void test_bad_crc_and_length() {
    uint8_t payload[CrabitWire::PAYLOAD_SIZE]{};
    uint8_t frame[CrabitWire::FRAME_SIZE]{};
    size_t payload_len = 0, frame_len = 0;
    assert(CrabitWire::encode_message("diag/echo", "ok", 1, payload,
                                      sizeof(payload), &payload_len));
    assert(CrabitWire::encode_frame(payload, payload_len, frame, sizeof(frame), &frame_len));
    frame[frame_len - 1] ^= 0x5A;
    uint8_t decoded[CrabitWire::PAYLOAD_SIZE]{};
    size_t decoded_len = 0;
    assert(!CrabitWire::decode_frame(frame, frame_len, decoded,
                                     sizeof(decoded), &decoded_len));
    frame[frame_len - 1] ^= 0x5A;
    frame[0] = 0xFF;
    frame[1] = 0x7F;
    assert(!CrabitWire::decode_frame(frame, frame_len, decoded,
                                     sizeof(decoded), &decoded_len));
}

struct ParserHarness {
    uint8_t buffer[CrabitWire::FRAME_SIZE]{};
    size_t used = 0;
    uint32_t partial_since = 0;
    bool partial_active = false;
    uint32_t resync = 0;
    uint32_t timeouts = 0;
    uint32_t drops = 0;
    std::deque<CrabitWire::Message> queue;

    void feed(const uint8_t* data, size_t length, uint32_t now) {
        for (size_t n = 0; n < length; ++n) {
            if (used >= sizeof(buffer)) { ++resync; used = 0; }
            if (used == 0) {
                partial_since = now;
                partial_active = true;
            }
            buffer[used++] = data[n];
            bool parsing = true;
            while (parsing && used >= 2) {
                const size_t declared = (size_t)buffer[0] | ((size_t)buffer[1] << 8);
                const size_t expected = declared + 4;
                if (declared < 5 || declared > CrabitWire::PAYLOAD_SIZE) {
                    memmove(buffer, buffer + 1, used - 1); --used; ++resync; continue;
                }
                if (used < expected) break;
                uint8_t payload[CrabitWire::PAYLOAD_SIZE]{};
                size_t payload_len = 0;
                CrabitWire::Message message{};
                if (CrabitWire::decode_frame(buffer, expected, payload, sizeof(payload), &payload_len) &&
                    CrabitWire::decode_message(payload, payload_len, &message)) {
                    if (queue.size() == 2) { ++drops; }
                    else queue.push_back(message);
                    memmove(buffer, buffer + expected, used - expected);
                    used -= expected;
                    partial_since = used == 0 ? 0 : now;
                    partial_active = used != 0;
                } else {
                    memmove(buffer, buffer + 1, used - 1); --used; ++resync;
                }
                parsing = used >= 2;
            }
        }
    }

    void tick(uint32_t now) {
        if (partial_active && now - partial_since > 250) {
            ++timeouts; ++resync; used = 0; partial_since = 0; partial_active = false;
        }
    }
};

static std::vector<uint8_t> make_frame(const char* topic, const char* value, uint16_t id) {
    std::vector<uint8_t> frame(CrabitWire::FRAME_SIZE);
    uint8_t payload[CrabitWire::PAYLOAD_SIZE]{};
    size_t payload_len = 0, frame_len = 0;
    assert(CrabitWire::encode_message(topic, value, id, payload, sizeof(payload), &payload_len));
    assert(CrabitWire::encode_frame(payload, payload_len, frame.data(), frame.size(), &frame_len));
    frame.resize(frame_len);
    return frame;
}

static void test_link_parser_harness() {
    const auto first = make_frame("diag/echo", "one", 1);
    const auto second = make_frame("sys/mode", "BLE", 2);
    ParserHarness parser;
    // One-byte fragmentation and a back-to-back frame.
    for (uint8_t byte : first) parser.feed(&byte, 1, 10);
    parser.feed(second.data(), second.size(), 20);
    assert(parser.queue.size() == 2);
    assert(parser.queue.front().request_id == 1);
    parser.queue.pop_front();
    // Corrupt frame followed by noise and a valid frame must resynchronize.
    auto corrupt = first;
    corrupt[corrupt.size() - 1] ^= 0x55;
    const uint8_t noise[] = {0xFF, 0xFF, 0x01};
    parser.feed(corrupt.data(), corrupt.size(), 30);
    parser.feed(noise, sizeof(noise), 30);
    parser.feed(first.data(), first.size(), 30);
    assert(parser.resync > 0);
    assert(parser.queue.size() == 2);
    // A partial frame expires even if no further bytes arrive.
    ParserHarness timeout_parser;
    timeout_parser.feed(first.data(), 3, 0);
    timeout_parser.tick(251);
    assert(timeout_parser.timeouts == 1 && timeout_parser.used == 0);
    // Queue saturation is counted rather than blocking the parser.
    ParserHarness full_parser;
    full_parser.feed(first.data(), first.size(), 1);
    full_parser.feed(second.data(), second.size(), 1);
    full_parser.feed(first.data(), first.size(), 1);
    assert(full_parser.drops == 1 && full_parser.queue.size() == 2);
}

int main() {
    test_round_trip();
    test_bad_crc_and_length();
    test_link_parser_harness();
    std::cout << "crabit wire transport tests passed\n";
    return 0;
}
