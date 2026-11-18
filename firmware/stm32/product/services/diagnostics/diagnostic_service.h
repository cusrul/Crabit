#pragma once

#include "crabit_wire.h"

// Diagnostic topics that are not part of command routing.
class DiagnosticService {
public:
    // Branches that must run before command dispatch.
    static bool handleEarly(const CrabitWire::Message& message);
    // Branches that must run after command/legacy dispatch and the UI dirty mark.
    static void handleLate(const CrabitWire::Message& message);
};
