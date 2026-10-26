#ifndef CRABIT_NETWORK_SYNC_H
#define CRABIT_NETWORK_SYNC_H

#include "src/CrabitBuildProfile.h"
#include "crabit_wire.h"

namespace NetworkSync {
void init();
bool enqueue(const CrabitWire::Message& message);
void tick();
}

#endif
