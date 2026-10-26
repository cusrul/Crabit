#pragma once
#include <stdint.h>
#include <string>
using String = std::string;
extern uint32_t auditFakeMillis;
inline uint32_t millis() {
    return auditFakeMillis;
}
