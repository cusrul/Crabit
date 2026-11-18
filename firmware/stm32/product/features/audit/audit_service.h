#pragma once

#include "crabit_wire.h"

// audit/* topics and the RAM audit store.
class AuditService {
public:
    static bool handle(const CrabitWire::Message& message);
};
