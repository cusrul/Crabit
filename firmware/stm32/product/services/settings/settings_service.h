#pragma once

#include "crabit_wire.h"

class SettingsService {
public:
    static bool handle(const CrabitWire::Message& message);
};
