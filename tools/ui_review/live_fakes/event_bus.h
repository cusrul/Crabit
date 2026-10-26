#pragma once
// Offline preview never sends a device command.
class EventBus {
   public:
    static void publish(const char*, const char*) {}
};
