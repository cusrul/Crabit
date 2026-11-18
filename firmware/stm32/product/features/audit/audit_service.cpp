#include "audit_service.h"

#include <stdio.h>
#include <string.h>

#include "audit_spool.h"
#include "comms_link.h"
#include "topics.h"

namespace {
CrabitAudit::Store audit_store;
}

bool AuditService::handle(const CrabitWire::Message& message) {
    if (strncmp(message.topic, Topics::AuditPrefix, 6) != 0)
        return false;
    char reply[CrabitWire::MAX_VALUE + 1];
    if (strcmp(message.topic, CrabitAudit::PutTopic) == 0)
        audit_store.put(message.request_id, message.value, reply, sizeof(reply));
    else if (strcmp(message.topic, CrabitAudit::ReadTopic) == 0)
        audit_store.read(message.value, reply, sizeof(reply));
    else if (strcmp(message.topic, CrabitAudit::AckTopic) == 0)
        audit_store.acknowledge(message.value, reply, sizeof(reply));
    else
        snprintf(reply, sizeof(reply), "unsupported");
    CrabitLink::send(message.topic, reply, message.request_id);
    return true;
}
