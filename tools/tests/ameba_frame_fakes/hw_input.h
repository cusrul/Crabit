#pragma once
#include <deque>
class FakeInput : public InputDriver {
public:
    int polls = 0;
    std::deque<UIEvent> events;
    UIEvent poll() override { ++polls; if (events.empty()) return UIEvent{EVT_NONE,0}; UIEvent e=events.front(); events.pop_front(); return e; }
};
