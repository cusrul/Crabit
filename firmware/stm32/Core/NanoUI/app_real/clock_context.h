#ifndef CLOCK_CONTEXT_H
#define CLOCK_CONTEXT_H

#include "NanoUI/core/text.h"
#include "rtc.h"

// 馃専 缁熶竴鐨勬暟鎹笂涓嬫枃锛岃〃鐩樺彧绠℃嬁鏁版嵁锛屼笉绠℃暟鎹€庝箞鏉?
struct ClockContext {
    RTC_TimeTypeDef* rtcTime;
    RTC_DateTypeDef* rtcDate;
    NanoUI::NanoUIText* hStr;
    NanoUI::NanoUIText* mStr;
    NanoUI::NanoUIText* sStr;
    NanoUI::NanoUIText* dateStr;
    NanoUI::NanoUIText* dayStr;
    NanoUI::NanoUIText* ramStr;
    NanoUI::NanoUIText* batPctStr;
    float* batProgress;
    bool* colon_blink;
};

#endif

