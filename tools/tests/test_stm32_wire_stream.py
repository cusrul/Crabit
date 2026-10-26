"""Host regression tests for the production STM32 CrabitWire RX loop."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_companion_uart_probe import function


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/stm32/Core/Src/nanoui_port.cpp"
LOOP_SIGNATURE = "for (uint16_t i = 0; i < total_len; i++)"


HARNESS_BEFORE_LOOP = r"""
#include "crabit_wire.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

uint8_t rx_stitch_buf[CrabitWire::FRAME_SIZE] = {};
uint16_t stitch_idx = 0;
uint8_t decoded[1024] = {};
uint32_t trace_bad_frames = 0, trace_frames = 0, trace_ui_drops = 0, dbg_rx_errs = 0;
size_t maximum_pending = 0;
std::vector<CrabitWire::Message> ui_messages;
size_t trace_messages = 0, trace_records = 0;

namespace Crabit {
namespace CompanionBle {
bool hardwareActive() { return false; }
}
namespace CompanionProbe {
struct Summary { uint32_t size, zero, ff, printable; };
Summary summarize(const uint8_t*, uint16_t) { return Summary{0, 0, 0, 0}; }
}
namespace CompanionTrace {
enum Event { TimeRx, TimeQueued };
void message(const char*, const char*, uint16_t) { ++trace_messages; }
void record(Event, uint16_t, uint32_t, uint32_t = 0) { ++trace_records; }
}
}
namespace EventBus {
bool enqueueMessage(const CrabitWire::Message& message) {
    ui_messages.push_back(message);
    return true;
}
}

void parse_chunk(uint8_t* raw_data, uint16_t total_len) {
"""

HARNESS_AFTER_LOOP = r"""
    maximum_pending = std::max(maximum_pending, static_cast<size_t>(stitch_idx));
    assert(stitch_idx < CrabitWire::FRAME_SIZE);
}

std::vector<uint8_t> frame_from_payload(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame(CrabitWire::FRAME_SIZE);
    size_t written = 0;
    assert(CrabitWire::encode_frame(payload.data(), payload.size(), frame.data(), frame.size(), &written));
    frame.resize(written);
    return frame;
}

std::vector<uint8_t> make_frame(const std::string& topic, const std::string& value, uint16_t id) {
    uint8_t payload[CrabitWire::PAYLOAD_SIZE] = {};
    size_t payload_len = 0;
    assert(CrabitWire::encode_message(topic.c_str(), value.c_str(), id, payload, sizeof(payload), &payload_len));
    return frame_from_payload(std::vector<uint8_t>(payload, payload + payload_len));
}

void feed_part(const std::vector<uint8_t>& bytes, size_t begin, size_t end, size_t chunk_size) {
    assert(begin <= end && end <= bytes.size() && chunk_size != 0);
    uint8_t raw_data[512];
    for (size_t pos = begin; pos < end;) {
        const size_t count = std::min(std::min(chunk_size, sizeof(raw_data)), end - pos);
        memcpy(raw_data, bytes.data() + pos, count);
        parse_chunk(raw_data, static_cast<uint16_t>(count));
        pos += count;
    }
}

void feed(const std::vector<uint8_t>& bytes, size_t chunk_size) {
    feed_part(bytes, 0, bytes.size(), chunk_size);
}

void append(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

void assert_message(size_t index, uint16_t id, const char* topic, const char* value) {
    assert(index < ui_messages.size());
    const CrabitWire::Message& message = ui_messages[index];
    assert(message.request_id == id);
    assert(strcmp(message.topic, topic) == 0);
    assert(strcmp(message.value, value) == 0);
}

std::vector<uint8_t> invalid_field_frame(uint8_t topic_len, uint8_t value_len, uint16_t id) {
    const size_t payload_len = 5U + topic_len + value_len;
    assert(payload_len <= CrabitWire::PAYLOAD_SIZE);
    std::vector<uint8_t> payload(payload_len, 'x');
    payload[0] = CrabitWire::VERSION;
    payload[1] = topic_len;
    payload[2] = value_len;
    payload[3] = static_cast<uint8_t>(id);
    payload[4] = static_cast<uint8_t>(id >> 8);
    return frame_from_payload(payload);
}

void boot_batch(int extra_prefix, size_t chunk_size) {
    std::vector<uint8_t> bytes(static_cast<size_t>(extra_prefix), static_cast<uint8_t>('A'));
    const std::vector<uint8_t> advertising = make_frame("sys/ble", "ADVERTISING", 1);
    assert(advertising.size() == 27);
    append(bytes, advertising);

    const size_t sizes[] = {33, 32, 31, 30};
    const char* stages[] = {"BLE_INIT_BEGIN", "BLE_INIT_DONE", "PERIPH_BEGIN", "PERIPH_DONE"};
    for (size_t i = 0; i < 4; ++i) {
        const std::vector<uint8_t> diagnostic = make_frame("diag/radio", stages[i], 1);
        assert(diagnostic.size() == sizes[i]);
        append(bytes, diagnostic);
    }
    assert(bytes.size() == static_cast<size_t>(extra_prefix) + 153);
    feed(bytes, chunk_size);
    assert(trace_frames == 5 && trace_messages == 5);
    assert(ui_messages.size() == 1);
    assert_message(0, 1, "sys/ble", "ADVERTISING");
    assert(stitch_idx == 0);
}

int main(int argc, char** argv) {
    assert(argc >= 2);
    const std::string mode(argv[1]);
    if (mode == "boot") {
        assert(argc == 4);
        boot_batch(std::atoi(argv[2]), static_cast<size_t>(std::atoi(argv[3])));
    } else if (mode == "splits") {
        const std::vector<uint8_t> exemplar = make_frame("sys/notify", "split", 1);
        for (size_t split = 1; split < exemplar.size(); ++split) {
            const uint16_t id = static_cast<uint16_t>(100 + split);
            const std::vector<uint8_t> one = make_frame("sys/notify", "split", id);
            assert(one.size() == exemplar.size());
            feed_part(one, 0, split, one.size());
            feed_part(one, split, one.size(), one.size());
            assert(stitch_idx == 0);
            assert(ui_messages.size() == split);
            assert_message(split - 1, id, "sys/notify", "split");
        }
        std::vector<uint8_t> joined;
        for (uint16_t id = 500; id < 505; ++id) append(joined, make_frame("sys/notify", "back-to-back", id));
        const size_t before = ui_messages.size();
        feed(joined, joined.size());
        assert(ui_messages.size() == before + 5);
        for (size_t i = 0; i < 5; ++i) assert_message(before + i, static_cast<uint16_t>(500 + i), "sys/notify", "back-to-back");
        std::vector<uint8_t> bytewise;
        for (uint16_t id = 600; id < 603; ++id) append(bytewise, make_frame("sys/notify", "bytewise", id));
        const size_t bytewise_before = ui_messages.size();
        feed(bytewise, 1);
        assert(ui_messages.size() == bytewise_before + 3);
        for (size_t i = 0; i < 3; ++i) assert_message(bytewise_before + i, static_cast<uint16_t>(600 + i), "sys/notify", "bytewise");
    } else if (mode == "maximum") {
        std::vector<uint8_t> noise(4096, static_cast<uint8_t>('A'));
        feed(noise, 1);
        const std::string topic(CrabitWire::MAX_TOPIC, 't');
        const std::string value(CrabitWire::MAX_VALUE, 'v');
        const std::vector<uint8_t> frame = make_frame(topic, value, 700);
        assert(frame.size() == CrabitWire::FRAME_SIZE);
        feed(frame, 1);
        assert(ui_messages.size() == 1 && trace_frames == 1);
        assert(ui_messages[0].request_id == 700);
        assert(ui_messages[0].topic_len == CrabitWire::MAX_TOPIC);
        assert(ui_messages[0].value_len == CrabitWire::MAX_VALUE);
        assert(maximum_pending < CrabitWire::FRAME_SIZE);
        assert(stitch_idx == 0);
    } else if (mode == "invalid") {
        std::vector<uint8_t> bad_crc = make_frame("sys/notify", "bad-crc", 801);
        bad_crc.back() ^= 0x80;
        std::vector<uint8_t> bad_version = make_frame("sys/notify", "bad-version", 802);
        bad_version[2] = static_cast<uint8_t>(CrabitWire::VERSION + 1);
        const size_t version_payload_len = bad_version.size() - 4;
        const uint16_t version_crc = CrabitWire::crc16(bad_version.data() + 2, version_payload_len);
        bad_version[bad_version.size() - 2] = static_cast<uint8_t>(version_crc);
        bad_version[bad_version.size() - 1] = static_cast<uint8_t>(version_crc >> 8);
        std::vector<uint8_t> bad_length = make_frame("sys/notify", "bad-length", 803);
        bad_length[0] = 4;
        bad_length[1] = 0;
        const std::vector<uint8_t> bad_topic = invalid_field_frame(
            static_cast<uint8_t>(CrabitWire::MAX_TOPIC + 1), 0, 804);
        const std::vector<uint8_t> bad_value = invalid_field_frame(
            1, static_cast<uint8_t>(CrabitWire::MAX_VALUE + 1), 805);
        const std::vector<std::vector<uint8_t>> invalids = {bad_crc, bad_version, bad_length, bad_topic, bad_value};
        for (size_t i = 0; i < invalids.size(); ++i) {
            feed(invalids[i], 512);
            feed(make_frame("sys/notify", "RECOVER", static_cast<uint16_t>(810 + i)), 512);
            assert(ui_messages.size() == i + 1);
            assert_message(i, static_cast<uint16_t>(810 + i), "sys/notify", "RECOVER");
        }
        assert(trace_frames == invalids.size());
        assert(trace_bad_frames > 0);
        assert(stitch_idx == 0);
    } else if (mode == "incomplete") {
        const std::vector<uint8_t> frame = make_frame("sys/notify", "once", 901);
        feed_part(frame, 0, frame.size() - 1, frame.size());
        assert(ui_messages.empty() && trace_frames == 0);
        assert(stitch_idx == frame.size() - 1);
        feed_part(frame, frame.size() - 1, frame.size(), 1);
        assert(ui_messages.size() == 1 && trace_frames == 1);
        feed(frame, frame.size());
        assert(ui_messages.size() == 2 && trace_frames == 2);
        assert_message(0, 901, "sys/notify", "once");
        assert_message(1, 901, "sys/notify", "once");
        assert(stitch_idx == 0);
    } else if (mode == "nested") {
        const std::vector<uint8_t> inner = make_frame("sys/notify", "inside", 999);
        const size_t value_len = inner.size() + 5;
        std::vector<uint8_t> payload(5 + strlen("diag/radio") + value_len, 'z');
        payload[0] = CrabitWire::VERSION;
        payload[1] = static_cast<uint8_t>(strlen("diag/radio"));
        payload[2] = static_cast<uint8_t>(value_len);
        payload[3] = 1;
        payload[4] = 0;
        memcpy(payload.data() + 5, "diag/radio", strlen("diag/radio"));
        memcpy(payload.data() + 5 + strlen("diag/radio") + 2, inner.data(), inner.size());
        std::vector<uint8_t> damaged_outer = frame_from_payload(payload);
        damaged_outer.back() ^= 1;
        feed(damaged_outer, 512);
        assert(trace_frames == 1 && ui_messages.size() == 1);
        assert_message(0, 999, "sys/notify", "inside");
        assert(trace_bad_frames > 0 && stitch_idx < CrabitWire::FRAME_SIZE);
    } else {
        assert(false && "unknown test mode");
    }
    return 0;
}
"""


class Stm32WireStreamTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("g++")
        if compiler is None:
            raise AssertionError("g++ is required for the STM32 wire-stream host test")
        source = SOURCE.read_text(encoding="utf-8")
        task = function(source, "void Task_Modem(void *pvParameters)")
        if task.count(LOOP_SIGNATURE) != 1:
            raise AssertionError("expected exactly one production Task_Modem byte loop")
        for declaration in (
            "static uint8_t rx_stitch_buf[CrabitWire::FRAME_SIZE];",
            "static uint16_t stitch_idx = 0;",
            "static uint8_t decoded[1024];",
        ):
            if declaration not in task:
                raise AssertionError(
                    f"production Task_Modem lost parser state declaration: {declaration}"
                )
        loop = function(task, LOOP_SIGNATURE)
        cls.temp = tempfile.TemporaryDirectory(prefix="crabit-stm32-wire-stream-")
        folder = Path(cls.temp.name)
        fixture = folder / "wire_stream.cpp"
        cls.binary = folder / "wire_stream.exe"
        fixture.write_text(HARNESS_BEFORE_LOOP + loop + HARNESS_AFTER_LOOP, encoding="utf-8")
        subprocess.run(
            [
                compiler,
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "firmware/shared"),
                str(fixture),
                "-o",
                str(cls.binary),
            ],
            check=True,
            cwd=ROOT,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, *args):
        subprocess.run([str(self.binary), *map(str, args)], check=True, cwd=ROOT)

    def test_startup_batch_survives_odd_even_ascii_prefixes(self):
        for prefix in (0, 1, 2, 633, 848):
            with self.subTest(extra_ascii_prefix=prefix):
                self.run_case("boot", prefix, 1 if prefix == 0 else 512)

    def test_every_frame_split_back_to_back_and_bytewise_input(self):
        self.run_case("splits")

    def test_maximum_legal_frame_and_bounded_pending_state(self):
        self.run_case("maximum")

    def test_invalid_crc_version_length_topic_and_value_recover(self):
        self.run_case("invalid")

    def test_incomplete_frame_waits_and_delivery_is_exactly_once(self):
        self.run_case("incomplete")

    def test_crc_failed_candidate_does_not_hide_nested_valid_frame(self):
        self.run_case("nested")


if __name__ == "__main__":
    unittest.main()
