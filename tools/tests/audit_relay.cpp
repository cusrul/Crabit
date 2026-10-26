#include "../../firmware/shared/audit_spool.h"
#include "../../firmware/rtl8720dn/NanoUI/AuditRelay.h"
#include <cassert>
#include <string>

uint32_t auditFakeMillis = 0;
static CrabitAudit::Store store;
static CrabitWire::Message reply{};
static unsigned writes = 0;
namespace Protocol {
bool send_message_checked(const char* topic, const char* value, uint16_t request) {
    assert(strcmp(topic, CrabitAudit::PutTopic) == 0);
    assert(strlen(value) <= CrabitWire::MAX_VALUE);
    ++writes;
    store.put(request, value, reply.value, sizeof(reply.value));
    reply.request_id = request;
    reply.value_len = uint8_t(strlen(reply.value));
    return true;
}
}  // namespace Protocol

int main() {
    AuditRelay::begin();
    const std::string record(150, 'x');
    assert(AuditRelay::enqueue(record.c_str()));
    AuditRelay::tick();
    assert(writes == 1);
    assert(std::string(reply.value) == "next:64");
    // Reject foreign request ACKs, then retry the first chunk after ACK loss.
    auto stale = reply;
    ++stale.request_id;
    AuditRelay::receive(stale);
    AuditRelay::tick();
    assert(writes == 1);
    auditFakeMillis += 750;
    AuditRelay::tick();
    assert(writes == 2);
    AuditRelay::receive(reply);
    AuditRelay::tick();
    assert(std::string(reply.value) == "next:128");
    AuditRelay::receive(reply);
    AuditRelay::tick();
    assert(std::string(reply.value) == "stored:1");
    auditFakeMillis += 750;
    AuditRelay::tick();  // lost final ACK: Store must not duplicate
    assert(std::string(reply.value) == "stored:1");
    AuditRelay::receive(reply);
    AuditRelay::tick();
    assert(AuditRelay::stored() == 1);
    assert(AuditRelay::dropped() == 0);
    assert(AuditRelay::enqueue("z"));
    AuditRelay::tick();
    AuditRelay::receive(reply);
    AuditRelay::tick();
    assert(AuditRelay::stored() == 2);
    assert(AuditRelay::enqueue("full"));
    AuditRelay::tick();
    AuditRelay::receive(reply);
    AuditRelay::tick();
    assert(AuditRelay::dropped() == 1);
    assert(std::string(AuditRelay::status()) == "主控记录已满");
    assert(!AuditRelay::enqueue(std::string(769, 'x').c_str()));
    assert(AuditRelay::dropped() == 2);
    assert(AuditRelay::enqueue("no reply"));
    AuditRelay::tick();
    for (int i = 0; i < 4; ++i) {
        auditFakeMillis += 750;
        AuditRelay::tick();
    }
    assert(AuditRelay::dropped() == 3);
    assert(std::string(AuditRelay::status()) == "主控接收超时");
}
