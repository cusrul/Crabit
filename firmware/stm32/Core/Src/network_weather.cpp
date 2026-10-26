#include "network_weather.h"
#include "NanoUI/NanoUI.h"
#include "event_bus.h"
#include "rtc.h"
#include "companion_trace.h"
#include <stdio.h>

namespace Crabit { namespace NetworkWeather {
namespace {
char ssid[33] = {}, password[65] = {}, stage[24] = "UNCONFIGURED";
CrabitWeather::Place place;
CrabitWeather::Day cached[CrabitWeather::MaxDays], pending[CrabitWeather::MaxDays];
unsigned fields = 0, received = 0, cachedCount = 0, step = 0;
uint16_t sid = 0;
uint32_t started = 0;
bool running = false, sendNext = false, clockValid = false, sessionTime = false;
bool localTimeRejected = false;
// The authenticated phone relay has its own transaction and cache. It never
// starts or mutates the legacy BW16 Wi-Fi session above.
CrabitWeather::Place phonePlace;
CrabitWeather::Day phoneCached[CrabitWeather::MaxDays], phonePending[CrabitWeather::MaxDays];
unsigned phoneCachedCount = 0, phoneReceived = 0;
uint32_t phoneUpdated = 0, phoneStarted = 0, phoneEpoch = 0;
int16_t phoneUpdatedOffset = 0;
uint32_t phoneToken = 0;
bool phoneActive = false, phoneHasTime = false, phoneClock = false;
char phoneStage[20] = "READY";
void state(const char* value) { snprintf(stage, sizeof(stage), "%s", value); }
void phoneState(const char* value) { snprintf(phoneStage, sizeof(phoneStage), "%s", value); }
void clearPhonePending() {
    for (auto& item:phonePending) item=CrabitWeather::Day{};
}
void expirePhoneTransaction() {
    if (phoneActive && uint32_t(HAL_GetTick()-phoneStarted)>=30000u) {
        phoneActive=false; phoneHasTime=false; phoneReceived=0; clearPhonePending(); phoneState("STALE");
    }
}
void fail(const char* value) {
    CompanionTrace::record(CompanionTrace::TimeResult, sid, sessionTime ? 1u : 0u, received);
    USB_LOG("[NET] id=%u result=%s last=%s rtc=%u days=%u\r\n", sid, value, stage,
            sessionTime ? 1u : 0u, received);
    running = false; sendNext = false; state(value);
}
bool send(const char* topic, const char* value) {
    if (EventBus::tryPublish(topic, value, sid)) return true;
    fail("TX_FAILED"); return false;
}
bool applyTime(const char* value, int16_t offset, bool updateLegacy) {
    auto rejected = [](uint32_t reason, uint32_t detail = 0) {
        CompanionTrace::record(CompanionTrace::TimeRejected, sid, reason, detail);
        return false;
    };
    if (!value || !*value) return rejected(1);
    for (const char* p = value; *p; ++p) if (*p < '0' || *p > '9') return rejected(1);
    errno = 0; char* end = nullptr; unsigned long epoch = strtoul(value, &end, 10);
    if (errno || !end || *end || epoch > UINT32_MAX) return rejected(2, errno);
    CrabitWeather::Civil c;
    if (!CrabitWeather::civil(uint32_t(epoch), offset, c)) return rejected(3, uint32_t(epoch));
    RTC_TimeTypeDef t{}; RTC_DateTypeDef d{};
    t.Hours = c.hour; t.Minutes = c.minute; t.Seconds = c.second;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE; t.StoreOperation = RTC_STOREOPERATION_RESET;
    d.Year = c.year % 100; d.Month = c.month; d.Date = c.day; d.WeekDay = c.weekday;
    const auto timeResult = HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
    const auto dateResult = timeResult == HAL_OK ? HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) : HAL_ERROR;
    USB_LOG("[NET] id=%u rtc_time=%d rtc_date=%d local=%04u-%02u-%02u %02u:%02u:%02u\r\n",
            sid, int(timeResult), int(dateResult), c.year, c.month, c.day, c.hour, c.minute, c.second);
    if (timeResult != HAL_OK || dateResult != HAL_OK)
        return rejected(4, (uint32_t(timeResult)<<8) | uint32_t(dateResult));
    RTC_TimeTypeDef readTime{}; RTC_DateTypeDef readDate{};
    const auto readTimeResult = HAL_RTC_GetTime(&hrtc, &readTime, RTC_FORMAT_BIN);
    // Reading date after time also unlocks the STM32 RTC shadow registers.
    const auto readDateResult = HAL_RTC_GetDate(&hrtc, &readDate, RTC_FORMAT_BIN);
    USB_LOG("[NET] id=%u rtc_read=%d,%d value=%04u-%02u-%02u %02u:%02u:%02u\r\n",
            sid, int(readTimeResult), int(readDateResult), 2000u+readDate.Year,
            readDate.Month, readDate.Date, readTime.Hours, readTime.Minutes, readTime.Seconds);
    if (readTimeResult != HAL_OK || readDateResult != HAL_OK)
        return rejected(5, (uint32_t(readTimeResult)<<8) | uint32_t(readDateResult));
    if (!updateLegacy) {
        auto matches = [&](const CrabitWeather::Civil& expected) {
            return readDate.Year == expected.year % 100 && readDate.Month == expected.month &&
                readDate.Date == expected.day && readTime.Hours == expected.hour &&
                readTime.Minutes == expected.minute && readTime.Seconds == expected.second;
        };
        if (!matches(c)) {
            CrabitWeather::Civil next{};
            if (epoch == UINT32_MAX || !CrabitWeather::civil(uint32_t(epoch+1),offset,next) || !matches(next))
                return rejected(6);
        }
    }
    CompanionTrace::record(CompanionTrace::TimeReadback, sid,
                          (2000u+readDate.Year)*10000 + uint32_t(readDate.Month)*100 + readDate.Date,
                          uint32_t(readTime.Hours)*10000 + uint32_t(readTime.Minutes)*100 + readTime.Seconds);
    if (updateLegacy) { clockValid = true; sessionTime = true; }
    CompanionTrace::record(CompanionTrace::TimeApplied, sid, uint32_t(epoch),
                          uint32_t(c.hour)*10000 + uint32_t(c.minute)*100 + c.second);
    if (updateLegacy) snprintf(GlobalStore.net_time, sizeof(GlobalStore.net_time), "%02u:%02u:%02u", c.hour, c.minute, c.second);
    return true;
}
bool parseUnsigned(const char* text, uint32_t maximum, uint32_t& result) {
    if (!text || !*text) return false;
    uint32_t value = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const uint32_t digit = uint32_t(*p - '0');
        if (value > (maximum - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    result = value; return true;
}
bool parseTokenPrefix(const char* payload, uint32_t& token, const char*& rest) {
    const char* comma = strchr(payload, ',');
    if (!comma || size_t(comma-payload) >= 12) return false;
    char field[12]{}; memcpy(field, payload, size_t(comma-payload));
    if (!parseUnsigned(field, 2147483647u, token) || token == 0) return false;
    rest = comma + 1; return true;
}
uint32_t nextDate(uint32_t date) {
    unsigned y=date/10000, m=date/100%100, d=date%100;
    if (++d > CrabitWeather::monthDays(y,m)) { d=1; if (++m>12) {m=1; ++y;} }
    return y*10000+m*100+d;
}
const char* handlePhone(const CrabitWire::Message& message) {
    const char* value = message.value;
    uint32_t token=0; const char* rest=nullptr;
    if (!strncmp(value,"begin=",6)) {
        if (phoneActive) return "BUSY";
        if (running || Crabit::amebaTransitionActive()) return "BUSY";
        if (!parseTokenPrefix(value+6,token,rest)) return "INVALID";
        long fields[4];
        if (!CrabitWeather::numbers(rest,fields,4) || fields[0]<-90000000L || fields[0]>90000000L ||
            fields[1]<-180000000L || fields[1]>180000000L || fields[2]<-720 || fields[2]>840 ||
            fields[3]<1 || fields[3]>long(CrabitWeather::MaxDays)) return "INVALID";
        phoneToken=token; phonePlace.latitude=fields[0]; phonePlace.longitude=fields[1];
        phonePlace.offset=fields[2]; phonePlace.days=fields[3]; phoneReceived=0;
        phoneHasTime=false; phoneStarted=HAL_GetTick();
        clearPhonePending();
        phoneActive=true; phoneState("RECEIVING"); return "OK";
    }
    if (!strncmp(value,"time=",5)) {
        if (!parseTokenPrefix(value+5,token,rest)) return "INVALID";
    } else if (!strncmp(value,"day=",4)) {
        if (!parseTokenPrefix(value+4,token,rest)) return "INVALID";
    } else if (!strncmp(value,"commit=",7) || !strncmp(value,"cancel=",7)) {
        if (!parseUnsigned(value+7,2147483647u,token) || !token) return "INVALID";
        rest="";
    } else return "INVALID";
    if (!phoneActive || token!=phoneToken) return "STALE";
    if (!strncmp(value,"cancel=",7)) {
        phoneActive=false; phoneHasTime=false; phoneReceived=0; clearPhonePending(); phoneState("CANCELLED"); return "OK";
    }
    if (!strncmp(value,"time=",5)) {
        uint32_t timeToken;
        if (!parseTokenPrefix(value+5,timeToken,rest) || timeToken!=phoneToken || phoneHasTime) return "INVALID";
        uint32_t epoch;
        if (!parseUnsigned(rest,UINT32_MAX,epoch)) return "INVALID";
        CrabitWeather::Civil civil;
        if (!CrabitWeather::civil(epoch,phonePlace.offset,civil)) return "INVALID";
        phoneEpoch=epoch; phoneHasTime=true; phoneState("RECEIVING"); return "OK";
    }
    if (!strncmp(value,"day=",4)) {
        uint32_t dayToken;
        if (!parseTokenPrefix(value+4,dayToken,rest) || dayToken!=phoneToken) return "INVALID";
        long fields[6];
        if (!CrabitWeather::numbers(rest,fields,6) || fields[0]<0 || fields[0]>=phonePlace.days ||
            fields[1]<20240101L || fields[1]>20991231L || !CrabitWeather::validDate(uint32_t(fields[1])) || fields[2]<0 || fields[2]>99 ||
            fields[3]<-1000 || fields[4]>1000 || fields[3]>fields[4] || fields[5]<0 || fields[5]>100 ||
            (phoneReceived & (1u<<fields[0]))) return "INVALID";
        phonePending[fields[0]]={uint32_t(fields[1]),uint16_t(fields[2]),int16_t(fields[3]),
                                 int16_t(fields[4]),uint8_t(fields[5])};
        phoneReceived |= 1u<<fields[0]; phoneState("RECEIVING"); return "OK";
    }
    if (!strncmp(value,"commit=",7)) {
        if (!phoneHasTime || phoneReceived != ((1u<<phonePlace.days)-1u)) return "INCOMPLETE";
        for (unsigned i=0;i<phonePlace.days;++i) {
            if (!CrabitWeather::validDate(phonePending[i].date) ||
                (i && phonePending[i].date!=nextDate(phonePending[i-1].date))) return "INCOMPLETE";
        }
        char epochText[11]; snprintf(epochText,sizeof(epochText),"%lu",(unsigned long)phoneEpoch);
        if (!applyTime(epochText,phonePlace.offset,false)) {
            // A HAL write can partially succeed before a later write/read fails.
            // Keep the forecast, but no longer claim that the RTC is verified.
            phoneClock = false;
            return "RTC_FAILED";
        }
        memcpy(phoneCached,phonePending,sizeof(phoneCached)); phoneCachedCount=phonePlace.days;
        phoneUpdated=phoneEpoch; phoneUpdatedOffset=phonePlace.offset;
        phoneClock=true; phoneActive=false; phoneState("UPDATED"); return "DONE";
    }
    return "INVALID";
}
}
void init() { EventBus::setNetworkHandler(receive); }
bool configured() { return fields == 7; }
bool busy() { return running; }
uint16_t session() { return sid; }
const char* status() { return stage; }
bool timeValid() { return clockValid; }
unsigned count() { return cachedCount; }
const CrabitWeather::Day& day(unsigned index) { return cached[index < cachedCount ? index : 0]; }
unsigned phoneCount() { return phoneCachedCount; }
const CrabitWeather::Day& phoneDay(unsigned index) { return phoneCached[index < phoneCachedCount ? index : 0]; }
uint32_t lastUpdated() { return phoneUpdated; }
int16_t lastUpdatedOffset() { return phoneUpdatedOffset; }
bool phoneClockValid() { return phoneClock; }
const char* phoneLabel() {
    expirePhoneTransaction();
    if (!strcmp(phoneStage,"READY")) return phoneCachedCount ? "手机天气" : "请在手机端刷新天气";
    if (!strcmp(phoneStage,"RECEIVING")) return "手机正在同步天气";
    if (!strcmp(phoneStage,"UPDATED")) return "天气已更新";
    if (!strcmp(phoneStage,"CLOCK_SET")) return "手机时间已校准";
    if (!strcmp(phoneStage,"CANCELLED")) return "同步已取消 保留缓存";
    if (!strcmp(phoneStage,"STALE")) return "同步超时 保留缓存";
    return "同步失败 保留缓存";
}
const char* condition(uint16_t code) {
    if (code == 0) return "晴";
    if (code <= 3) return "多云";
    if (code == 45 || code == 48) return "雾";
    if (code >= 95) return "雷雨";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "雪";
    if (code >= 51 && code <= 82) return "雨";
    return "未知";
}
const char* label() {
    if (!strcmp(stage,"UNCONFIGURED")) return "请先配置网络与地区";
    if (!strcmp(stage,"CONFIGURED")) return "配置就绪";
    if (!strcmp(stage,"BOOT")) return "正在启动无线";
    if (!strcmp(stage,"CONFIGURING")) return "正在下发配置";
    if (!strcmp(stage,"CONNECTING")) return "正在连接网络";
    if (!strcmp(stage,"WIFI_ON")) return "网络已连接";
    if (!strcmp(stage,"TIME_OK")) return "校时成功 正在取天气";
    if (!strcmp(stage,"NTP_REQUEST")) return "正在获取网络时间";
    if (!strcmp(stage,"TIME_UNAVAILABLE")) return "校时失败 继续取天气";
    if (!strcmp(stage,"WEATHER_ONLY")) return "天气已更新 时间未校准";
    if (!strcmp(stage,"TIME_PENDING")) return "等待时间回包";
    if (!strcmp(stage,"HTTPS_CONNECT")) return "天气 建立安全连接";
    if (!strcmp(stage,"HTTP_READ")) return "天气 读取响应";
    if (!strcmp(stage,"HTTP_SEND")) return "天气 发送请求";
    if (!strcmp(stage,"HTTP_HEADERS")) return "天气 读取响应头";
    if (!strcmp(stage,"HTTP_BODY")) return "天气 读取正文";
    if (!strcmp(stage,"WEATHER_CLOSE")) return "天气 关闭连接";
    if (!strcmp(stage,"WEATHER_PARSE")) return "天气 解析数据";
    if (!strcmp(stage,"DONE")) return "天气与时间更新完成";
    if (!strcmp(stage,"CANCELLED")) return "已取消 保留上次天气";
    if (!strcmp(stage,"TIMEOUT")) return sessionTime ? "已校时 天气超时" : "更新超时";
    if (!strcmp(stage,"UART_RX_FAILED")) return "串口接收失败";
    if (!strcmp(stage,"TX_FAILED")) return "串口发送失败";
    if (!strcmp(stage,"WIFI_FAILED")) return "网络连接失败";
    if (!strcmp(stage,"TIME_FAILED")) return "网络校时失败";
    if (!strcmp(stage,"HTTP_FAILED")) return sessionTime ? "已校时 天气请求失败" : "天气请求失败";
    if (!strcmp(stage,"PARSE_FAILED")) return "天气数据无效";
    if (!strcmp(stage,"INCOMPLETE")) return "天气数据不完整";
    return "更新失败 请查看诊断";
}
bool begin() {
    if (!configured() || running) return false;
    if (++sid == 0) ++sid;
    running = true; sendNext = false; sessionTime = false; localTimeRejected = false;
    step = received = 0; started = HAL_GetTick();
    for (auto& item : pending) item = CrabitWeather::Day{};
    state("BOOT"); return true;
}
void cancel() { if (running) fail("CANCELLED"); }
void abort(const char* reason) { if (running) fail(reason); }
bool receive(const CrabitWire::Message& message) {
    if (!strcmp(message.topic,"app/weather")) {
        expirePhoneTransaction();
        if (!message.request_id) return true;
        if (!strcmp(message.value,"read")) {
            char summary[64];
            snprintf(summary,sizeof(summary),"count=%u;updated=%lu;clock=%u",phoneCachedCount,
                     (unsigned long)phoneUpdated,phoneClock?1u:0u);
            EventBus::publish("app/weather",summary,message.request_id); return true;
        }
        if (!strncmp(message.value,"clock=",6)) {
            if (phoneActive || running || Crabit::amebaTransitionActive()) {
                EventBus::publish("app/weather","BUSY",message.request_id); return true;
            }
            const char* comma=strchr(message.value+6,',');
            uint32_t epoch=0; int offset=0; char offsetText[8]{};
            bool valid=comma && size_t(comma-(message.value+6))<12 &&
                size_t(strlen(comma+1))<sizeof(offsetText);
            if (valid) {
                char epochText[12]{}; memcpy(epochText,message.value+6,size_t(comma-(message.value+6)));
                memcpy(offsetText,comma+1,strlen(comma+1));
                bool negative=false; const char* digits=offsetText;
                if (*digits=='-') { negative=true; ++digits; }
                uint32_t magnitude=0;
                valid=parseUnsigned(epochText,UINT32_MAX,epoch) && parseUnsigned(digits,840,magnitude);
                const long parsed=negative ? -long(magnitude) : long(magnitude);
                CrabitWeather::Civil civil{};
                valid=valid && parsed>=-720 && parsed<=840 && CrabitWeather::civil(epoch,int16_t(parsed),civil);
                offset=int(parsed);
            }
            if (!valid) { EventBus::publish("app/weather","INVALID",message.request_id); return true; }
            char epochText[11]; snprintf(epochText,sizeof(epochText),"%lu",(unsigned long)epoch);
            const bool applied=applyTime(epochText,int16_t(offset),false);
            if (applied) { phoneClock=true; phoneState("CLOCK_SET"); }
            else { phoneClock=false; phoneState("RTC_FAILED"); }
            EventBus::publish("app/weather",applied?"DONE":"RTC_FAILED",message.request_id); return true;
        }
        const char* result=handlePhone(message);
        if (result) {
            if (strcmp(result,"BUSY") && strcmp(result,"INCOMPLETE") &&
                strcmp(result,"OK") && strcmp(result,"DONE")) phoneState(result);
            EventBus::publish("app/weather",result,message.request_id);
        }
        return true;
    }
    if (!strcmp(message.topic, "app/net")) {
        const char* reply = "INVALID";
        if (!message.request_id) return true;
        if (running || Crabit::amebaTransitionActive()) reply = "BUSY";
        else if (!strcmp(message.value, "read")) {
            char summary[64];
            snprintf(summary, sizeof(summary), "configured=%u;days=%u", configured() ? 1u : 0u, place.days);
            EventBus::publish("app/net", summary, message.request_id); return true;
        } else if (!strncmp(message.value, "ssid=", 5)) {
            char draft[sizeof(ssid)] = {};
            if (CrabitWeather::unhex(message.value+5, draft, sizeof(draft))) {
                memcpy(ssid, draft, sizeof(ssid)); fields |= 1; reply = "OK";
            }
        } else if (!strncmp(message.value, "pass=", 5)) {
            char draft[sizeof(password)] = {};
            if (CrabitWeather::unhex(message.value+5, draft, sizeof(draft), true)) {
                memcpy(password, draft, sizeof(password)); fields |= 2; reply = "OK";
            }
        } else if (!strncmp(message.value, "place=", 6)) {
            CrabitWeather::Place draft;
            if (CrabitWeather::parsePlace(message.value+6, draft)) {
                if (place.latitude != draft.latitude || place.longitude != draft.longitude) cachedCount = 0;
                place = draft; fields |= 4; reply = "OK";
            }
        }
        if (!strcmp(reply,"OK")) state(configured() ? "CONFIGURED" : "UNCONFIGURED");
        EventBus::publish("app/net", reply, message.request_id); return true;
    }
    if (strncmp(message.topic, "net/", 4)) return false;
    const bool timeFrame = !strcmp(message.topic, "net/time");
    if (timeFrame) CompanionTrace::record(CompanionTrace::TimeHandled, message.request_id, step,
                                        (uint32_t(sid)<<1) | (running ? 1u : 0u));
    if (!running || message.request_id != sid) {
        if (timeFrame) CompanionTrace::record(CompanionTrace::TimeRejected, message.request_id, 6, sid);
        return true;
    }
    if (!strcmp(message.topic,"net/status")) {
        USB_LOG("[NET] id=%u stage=%s\r\n", sid, message.value);
        if (!strcmp(message.value,"WIFI_MODE")) {
            if (step == 0) { step = 1; sendNext = true; state("CONFIGURING"); }
        }
        else if (!strncmp(message.value,"CFG_OK:",7)) {
            const char* keys[] = {"", "ssid", "pass", "place"};
            if (step >= 1 && step <= 3 && !sendNext && !strcmp(message.value+7, keys[step])) { ++step; sendNext = true; }
        }
        else if (!strcmp(message.value,"DONE")) {
            if (step != 5 || received != ((1u << place.days)-1)) fail("INCOMPLETE");
            else {
                for (unsigned i = 1; i < place.days; ++i) if (pending[i].date <= pending[i-1].date) { fail("INCOMPLETE"); return true; }
                memcpy(cached, pending, sizeof(cached)); cachedCount = place.days;
                fail(sessionTime ? "DONE" : "WEATHER_ONLY");
            }
        } else if (!strcmp(message.value,"TIME_OK"))
            state(sessionTime ? "TIME_OK" : localTimeRejected ? "TIME_UNAVAILABLE" : "TIME_PENDING");
        else if (!strcmp(message.value,"TIME_UNAVAILABLE")) state("TIME_UNAVAILABLE");
        else if (!strcmp(message.value,"CONNECTING") || !strcmp(message.value,"WIFI_ON")) state(message.value);
        else if (strcmp(message.value,"QUEUED") != 0) fail(message.value);
    } else if (!strcmp(message.topic,"net/progress") && step == 5) {
        if (!strcmp(message.value,"NTP_REQUEST") || !strcmp(message.value,"HTTPS_CONNECT") || !strcmp(message.value,"HTTP_READ") ||
            !strcmp(message.value,"HTTP_SEND") || !strcmp(message.value,"HTTP_HEADERS") ||
            !strcmp(message.value,"HTTP_BODY") || !strcmp(message.value,"WEATHER_CLOSE") ||
            !strcmp(message.value,"WEATHER_PARSE")) {
            USB_LOG("[NET] id=%u progress=%s rtc=%u\r\n", sid, message.value, sessionTime ? 1u : 0u);
            state(message.value);
        }
    } else if (!strcmp(message.topic,"net/detail") && step == 5) {
        unsigned bytes, ms, ok, code, attempt, reason; int socket; unsigned long epoch; char extra;
        // Numeric-only diagnostics: never forward arbitrary UART/server text.
        if (sscanf(message.value,"bytes=%u;ms=%u;ok=%u%c", &bytes, &ms, &ok, &extra) == 3)
            USB_LOG("[NET] id=%u bytes=%u ms=%u ok=%u\r\n", sid, bytes, ms, ok);
        else if (sscanf(message.value,"http=%u%c", &code, &extra) == 1)
            USB_LOG("[NET] id=%u http=%u\r\n", sid, code);
        else if (sscanf(message.value,"ntp=%u;reason=%u;epoch=%lu%c", &attempt, &reason, &epoch, &extra) == 3)
            USB_LOG("[NET] id=%u ntp=%u reason=%u epoch=%lu\r\n", sid, attempt, reason, epoch);
        else if (sscanf(message.value,"ntp_socket=%d%c", &socket, &extra) == 1)
            USB_LOG("[NET] id=%u ntp_socket=%d\r\n", sid, socket);
    } else if (timeFrame) {
        if (step != 5) { CompanionTrace::record(CompanionTrace::TimeRejected, sid, 7, step); return true; }
        sessionTime = false;
        if (!applyTime(message.value, place.offset, true)) {
            localTimeRejected = true; state("TIME_UNAVAILABLE");
            USB_LOG("[NET] id=%u stage=RTC_FAILED\r\n", sid);
        } else {
            localTimeRejected = false;
            if (!strcmp(stage,"TIME_PENDING")) state("TIME_OK");
        }
    } else if (!strcmp(message.topic,"net/day") && step == 5) {
        unsigned i; CrabitWeather::Day next;
        if (!CrabitWeather::parseDay(message.value, i, next) || i >= place.days || received & (1u << i)) fail("PARSE_FAILED");
        else { pending[i] = next; received |= 1u << i; }
    }
    return true;
}
void tick() {
    if (!running) return;
    if (uint32_t(HAL_GetTick()-started) >= 75000) { fail("TIMEOUT"); return; }
    if (!sendNext) return;
    sendNext = false;
    if (step == 1) send("net/ssid", ssid);
    else if (step == 2) send("net/pass", password);
    else if (step == 3) {
        char value[80]; snprintf(value, sizeof(value), "%ld,%ld,%d,%u", (long)place.latitude, (long)place.longitude, place.offset, place.days);
        send("net/place", value);
    } else if (step == 4) { step = 5; send("net/run", "START"); }
}
} }
