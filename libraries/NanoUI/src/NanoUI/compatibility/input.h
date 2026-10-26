#ifndef HAL_INPUT_H
#define HAL_INPUT_H

enum EventType {
    EVT_NONE = 0,
    EVT_UP,
    EVT_DOWN,
    EVT_LEFT,
    EVT_RIGHT,
    EVT_OK,
    EVT_BACK,
    EVT_EXIT,
    EVT_OK_LONG // 确认键长按事件，供兼容输入驱动使用。
};

struct UIEvent {
    EventType type;
    int param;
};

class InputDriver {
public:
    virtual ~InputDriver() {}
    virtual void init() = 0;
    virtual UIEvent poll() = 0;
};

#endif
