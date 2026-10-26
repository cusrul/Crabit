#pragma once
namespace Crabit { namespace CompanionBle {
bool adoptModeBle();
bool active();
bool transitioning();
bool stop();
void resumeAfterUi();
void finishModeBleStart(const char* result);
} }
