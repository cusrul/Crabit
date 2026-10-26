#include "../../firmware/shared/product_preferences.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

using CrabitPreferences::Patch;
using CrabitPreferences::Record;

bool samePatch(const Patch& left, const Patch& right) {
    return left.fields == right.fields &&
           std::memcmp(left.values, right.values, sizeof(left.values)) == 0;
}

void expectDefaults() {
    using namespace CrabitPreferences;
    assert(enabled(Pat));
    assert(enabled(Qmi));
    assert(enabled(Sound));
    assert(enabled(Led));
    assert(current.volume == 100);
    assert(current.led_level == 8);
}

void testMissingAndCorruptOptionalRecords() {
    using namespace CrabitPreferences;

    static_assert(sizeof(Record) == 8);
    // No optional trailer leaves the inline legacy-compatible defaults active.
    expectDefaults();

    load(Record{});
    expectDefaults();

    Record corrupt_crc{};
    corrupt_crc.flags = 0;
    corrupt_crc.volume = 17;
    corrupt_crc.checksum = checksum(corrupt_crc);
    corrupt_crc.checksum ^= 1;
    assert(!valid(corrupt_crc));
    load(corrupt_crc);
    expectDefaults();

    Record corrupt_version = sealed();
    corrupt_version.magic ^= 0x00010000u;
    corrupt_version.checksum = checksum(corrupt_version);
    assert(!valid(corrupt_version));
    load(corrupt_version);
    expectDefaults();

    current.flags = static_cast<uint8_t>(Pat | Sound);
    current.volume = 43;
    current.led_level = 21;
    const Record saved = sealed();
    assert(valid(saved));
    current = Record{};
    load(saved);
    assert(enabled(Pat));
    assert(!enabled(Qmi));
    assert(enabled(Sound));
    assert(!enabled(Led));
    assert(current.volume == 43);
    assert(current.led_level == 21);
}

void testBoundedAtomicPatchParsing() {
    using namespace CrabitPreferences;

    Patch output{};
    output.fields = 0x155;
    for (unsigned i = 0; i < Count; ++i)
        output.values[i] = static_cast<uint8_t>(i + 10);
    const Patch original = output;

    assert(!parse(nullptr, 1, output));
    assert(samePatch(output, original));
    assert(!parse("", 0, output));
    assert(samePatch(output, original));

    char at_limit[CrabitWire::MAX_VALUE];
    std::memcpy(at_limit, "volume=", 7);
    std::memset(at_limit + 7, '0', sizeof(at_limit) - 7);
    Patch boundary{};
    assert(parse(at_limit, sizeof(at_limit), boundary));
    assert(boundary.fields == (1u << VolumeKey));
    assert(boundary.values[VolumeKey] == 0);

    char over_limit[CrabitWire::MAX_VALUE + 1];
    std::memcpy(over_limit, at_limit, sizeof(at_limit));
    over_limit[sizeof(at_limit)] = '0';
    assert(!parse(over_limit, sizeof(over_limit), output));
    assert(samePatch(output, original));

    const char* invalid[] = {
        "pat=0;pat=1",      "unknown=1", "qmi=2",  "volume=101",
        "led_level=;pat=1", "sound=-1",  "pat=1;", "=1",
    };
    for (const char* text : invalid) {
        assert(!parse(text, std::strlen(text), output));
        assert(samePatch(output, original));
    }

    const char valid_patch[] = "pat=0;qmi=1;volume=0;led_level=100;lift=1;face=4";
    Patch patch{};
    assert(parse(valid_patch, sizeof(valid_patch) - 1, patch));
    assert((patch.fields & (1u << PatKey)) != 0);
    assert((patch.fields & (1u << QmiKey)) != 0);
    assert(patch.values[PatKey] == 0);
    assert(patch.values[QmiKey] == 1);
    assert(patch.values[VolumeKey] == 0);
    assert(patch.values[LedLevelKey] == 100);

    current = Record{};
    applyRecord(patch);
    assert(!enabled(Pat));
    assert(enabled(Qmi));
    assert(enabled(Sound));
    assert(enabled(Led));
    assert(current.volume == 0);
    assert(current.led_level == 100);
}

void testReplyWireLimit() {
    using namespace CrabitPreferences;

    char reply[CrabitWire::MAX_VALUE + 1]{};
    assert(response(reply, sizeof(reply), "ok", "", true, false, UINT32_MAX, true));
    assert(std::strlen(reply) <= CrabitWire::MAX_VALUE);

    const std::string oversized_result(CrabitWire::MAX_VALUE + 1, 'x');
    assert(!response(reply, sizeof(reply), oversized_result.c_str(), "", false, false, 0, false));
}

}  // namespace

int main() {
    testMissingAndCorruptOptionalRecords();
    testBoundedAtomicPatchParsing();
    testReplyWireLimit();
    return 0;
}
