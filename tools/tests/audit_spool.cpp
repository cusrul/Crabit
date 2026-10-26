#include "../../firmware/shared/audit_spool.h"
#include <cassert>
#include <string>

int main() {
    using namespace CrabitAudit;
    Store store;
    char reply[192];
    auto put = [&](unsigned request, unsigned offset, unsigned total, const std::string& data) {
        char value[180], encoded[129];
        hex(data.data(), data.size(), encoded);
        snprintf(value, sizeof(value), "%u:%u:%s", offset, total, encoded);
        store.put(uint16_t(request), value, reply, sizeof(reply));
        return std::string(reply);
    };
    const std::string payload(150, 'x');
    assert(put(1, 0, 150, payload.substr(0, 64)) == "next:64");
    assert(put(1, 0, 150, payload.substr(0, 64)) == "next:64");  // lost ACK
    assert(put(1, 65, 150, "x") == "invalid");
    assert(put(1, 64, 150, payload.substr(64, 64)) == "next:128");
    assert(put(1, 128, 150, payload.substr(128)) == "stored:1");
    assert(put(1, 128, 150, payload.substr(128)) == "stored:1");
    store.read("head", reply, sizeof(reply));
    assert(std::string(reply) == "id=1;size=150;pending=1;dropped=0");
    store.read("1:128", reply, sizeof(reply));
    assert(strlen(reply) == 44);
    store.read("2:0", reply, sizeof(reply));
    assert(std::string(reply) == "invalid");
    assert(put(2, 0, 1, "z") == "stored:2");
    assert(put(3, 0, 1, "z") == "full");
    store.acknowledge("2", reply, sizeof(reply));
    assert(std::string(reply) == "invalid");  // cannot skip head
    store.acknowledge("1", reply, sizeof(reply));
    assert(std::string(reply) == "ok");
    store.acknowledge("1", reply, sizeof(reply));
    assert(std::string(reply) == "ok");  // lost delete ACK
    store.acknowledge("2", reply, sizeof(reply));
    store.read("head", reply, sizeof(reply));
    assert(std::string(reply) == "id=0;size=0;pending=0;dropped=1");
    assert(put(4, 0, 768, std::string(64, 'a')) == "next:64");
    // A restarted BW16 may reuse a source ID: don't poison the next record.
    assert(put(4, 0, 768, std::string(64, 'b')) == "next:64");
    assert(put(4, 64, 768, std::string(64, 'b')) == "next:128");
    assert(put(4, 0, 768, std::string(64, 'b')) == "next:64");
    for (unsigned offset = 64; offset < 768; offset += 64) {
        const auto result = put(4, offset, 768, std::string(64, 'b'));
        assert(result == (offset == 704 ? "stored:3" : "next:" + std::to_string(offset + 64)));
    }
    // Same source ID and final bytes, but a new prefix: must be a new record.
    assert(put(4, 0, 768, std::string(64, 'c')) == "next:64");
    for (unsigned offset = 64; offset < 768; offset += 64) {
        const auto result = put(4, offset, 768, std::string(64, 'b'));
        assert(result == (offset == 704 ? "stored:4" : "next:" + std::to_string(offset + 64)));
    }
    for (const char* bad : {"", "-1:2:AB", "0:769:41", "0:1:GG", "0:1:00", "0:1:4",
                            "4294967296:1:41", "0:1:4141", "1:1:41"}) {
        store.put(5, bad, reply, sizeof(reply));
        assert(std::string(reply) == "invalid");
    }
    store.read("3:768", reply, sizeof(reply));
    assert(std::string(reply) == "invalid");
    store.read("3:42949672960", reply, sizeof(reply));
    assert(std::string(reply) == "invalid");
    return 0;
}
