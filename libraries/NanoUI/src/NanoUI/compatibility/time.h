#ifndef HAL_TIME_H
#define HAL_TIME_H

#include <stdint.h>

typedef uint32_t (*GetTicksFunc)();
typedef void (*DelayFunc)(uint32_t); // 平台延时回调，参数单位为毫秒。

class HAL_Time {
public:
    static void init(GetTicksFunc tFunc, DelayFunc dFunc) {
        getTicksImpl() = tFunc;
        getDelayImpl() = dFunc;
    }

    static uint32_t getTicks() {
        if (getTicksImpl()) return getTicksImpl()();
        return 0;
    }

    // 委托平台执行延时；未注册回调时不等待。
    static void delay(uint32_t ms) {
        if (getDelayImpl()) getDelayImpl()(ms);
    }

private:
    static GetTicksFunc& getTicksImpl() { static GetTicksFunc impl = nullptr; return impl; }
    static DelayFunc& getDelayImpl() { static DelayFunc impl = nullptr; return impl; }
};

inline uint32_t hal_get_ticks() { return HAL_Time::getTicks(); }
inline void hal_delay(uint32_t ms) { HAL_Time::delay(ms); } // 暴露给 App 层使用

#endif
