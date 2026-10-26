#ifndef AMEBA_EVENT_BUS_H
#define AMEBA_EVENT_BUS_H

// 避免 Arduino 的 min/max 宏展开干扰 C++ 标准库声明。
#undef max
#undef min
#include <vector>

typedef void (*TopicHandler)(const char* value);

struct Subscription {
    String topic;
    TopicHandler handler;
};

class AmebaEventBus {
private:
    static std::vector<Subscription> subs;

public:
    static void subscribe(const String& topic, TopicHandler handler) {
        subs.push_back({topic, handler});
    }

    static void dispatch(const char* topic, const char* value) {
        if (!topic || !value) return;
        for (auto& sub : subs) {
            if (sub.topic == topic) {
                sub.handler(value);
            }
        }
    }
};

#endif
