"""Host tests for the real STM32 network-weather state machine.

The production .cpp, public header, wire header, and portable weather contract
are copied byte-for-byte into a temporary firmware-shaped tree. Only its HAL,
RTC, NanoUI, and EventBus dependencies are replaced with small host doubles.
This proves state-machine and contract behavior, not STM32 scheduling or device
integration.
"""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREFERRED_COMPILER = r"C:\w64dev\w64devkit\bin\g++.exe"


MOCK_GLOBAL_STORE = r"""
#pragma once
struct GlobalDataStore { char net_time[16] = {}; };
extern GlobalDataStore GlobalStore;
"""

MOCK_NANOUI = r"""
#pragma once
#include "global_store.h"
"""

MOCK_EVENT_BUS = r"""
#pragma once
#include <stdint.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include "network_weather.h"

struct HostPublication {
    std::string topic;
    std::string value;
    uint16_t request_id;
};

namespace HostEventBus {
extern std::vector<HostPublication> published;
extern std::vector<HostPublication> transmitted;
extern bool reject_next_transmit;
extern bool (*network_handler)(const CrabitWire::Message&);
}

namespace Crabit { bool amebaTransitionActive(); }
void hostUsbLog(const char* format, ...);
#define USB_LOG(...) hostUsbLog(__VA_ARGS__)

namespace EventBus {
inline void setNetworkHandler(bool (*handler)(const CrabitWire::Message&)) {
    HostEventBus::network_handler = handler;
}
inline void publish(const char* topic, const char* value, uint16_t request_id) {
    HostEventBus::published.push_back({topic, value, request_id});
}
inline bool tryPublish(const char* topic, const char* value, uint16_t request_id) {
    if (HostEventBus::reject_next_transmit) {
        HostEventBus::reject_next_transmit = false;
        return false;
    }
    HostEventBus::transmitted.push_back({topic, value, request_id});
    return true;
}
}
"""

MOCK_RTC = r"""
#pragma once
#include <stdint.h>
struct RTC_HandleTypeDef {};
struct RTC_TimeTypeDef {
    uint8_t Hours = 0, Minutes = 0, Seconds = 0;
    uint32_t DayLightSaving = 0, StoreOperation = 0;
};
struct RTC_DateTypeDef {
    uint8_t Year = 0, Month = 0, Date = 0, WeekDay = 0;
};
constexpr int HAL_OK = 0, HAL_ERROR = 1;
constexpr uint32_t RTC_FORMAT_BIN = 0;
constexpr uint32_t RTC_DAYLIGHTSAVING_NONE = 0;
constexpr uint32_t RTC_STOREOPERATION_RESET = 0;
extern RTC_HandleTypeDef hrtc;
uint32_t HAL_GetTick();
int HAL_RTC_SetTime(RTC_HandleTypeDef*, RTC_TimeTypeDef*, uint32_t);
int HAL_RTC_SetDate(RTC_HandleTypeDef*, RTC_DateTypeDef*, uint32_t);
int HAL_RTC_GetTime(RTC_HandleTypeDef*, RTC_TimeTypeDef*, uint32_t);
int HAL_RTC_GetDate(RTC_HandleTypeDef*, RTC_DateTypeDef*, uint32_t);
"""

MOCK_COMPANION_TRACE = r"""
#pragma once
#include <stdint.h>
namespace Crabit { namespace CompanionTrace {
enum Event : uint16_t { TimeHandled, TimeRejected, TimeApplied, TimeReadback, TimeResult };
struct Record { Event event; uint16_t session; uint32_t a, b; };
void record(Event event, uint16_t session = 0, uint32_t a = 0, uint32_t b = 0);
} }
"""


HOST_DRIVER = r"""
#include "network_weather.h"
#include "event_bus.h"
#include "rtc.h"
#include "global_store.h"
#include "companion_trace.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

uint32_t fake_tick = 1000;
RTC_HandleTypeDef hrtc{};
GlobalDataStore GlobalStore{};
RTC_TimeTypeDef last_rtc_time{};
RTC_DateTypeDef last_rtc_date{};
unsigned rtc_time_calls = 0, rtc_date_calls = 0;
int rtc_time_result = HAL_OK, rtc_date_result = HAL_OK;
int rtc_get_time_result = HAL_OK, rtc_get_date_result = HAL_OK;
std::vector<std::string> usb_logs;
std::vector<Crabit::CompanionTrace::Record> trace_records;

namespace HostEventBus {
std::vector<HostPublication> published;
std::vector<HostPublication> transmitted;
bool reject_next_transmit = false;
bool (*network_handler)(const CrabitWire::Message&) = nullptr;
}
namespace Crabit {
bool transition_active = false;
bool amebaTransitionActive() { return transition_active; }
}
namespace Crabit { namespace CompanionTrace {
void record(Event event, uint16_t session, uint32_t a, uint32_t b) {
    trace_records.push_back({event, session, a, b});
}
} }

uint32_t HAL_GetTick() { return fake_tick; }
int HAL_RTC_SetTime(RTC_HandleTypeDef*, RTC_TimeTypeDef* value, uint32_t) {
    ++rtc_time_calls; last_rtc_time = *value; return rtc_time_result;
}
int HAL_RTC_SetDate(RTC_HandleTypeDef*, RTC_DateTypeDef* value, uint32_t) {
    ++rtc_date_calls; last_rtc_date = *value; return rtc_date_result;
}
int HAL_RTC_GetTime(RTC_HandleTypeDef*, RTC_TimeTypeDef* value, uint32_t) {
    *value = last_rtc_time; return rtc_get_time_result;
}
int HAL_RTC_GetDate(RTC_HandleTypeDef*, RTC_DateTypeDef* value, uint32_t) {
    *value = last_rtc_date; return rtc_get_date_result;
}
void hostUsbLog(const char* format, ...) {
    char buffer[256]{};
    va_list args; va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    usb_logs.emplace_back(buffer);
}

using Crabit::NetworkWeather::begin;
using Crabit::NetworkWeather::busy;
using Crabit::NetworkWeather::cancel;
using Crabit::NetworkWeather::count;
using Crabit::NetworkWeather::day;
using Crabit::NetworkWeather::init;
using Crabit::NetworkWeather::receive;
using Crabit::NetworkWeather::session;
using Crabit::NetworkWeather::status;
using Crabit::NetworkWeather::tick;
using HostEventBus::published;
using HostEventBus::transmitted;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
const Crabit::CompanionTrace::Record& traceAt(Crabit::CompanionTrace::Event event, size_t occurrence = 0) {
    for (const auto& record : trace_records) {
        if (record.event == event && occurrence-- == 0) return record;
    }
    throw std::runtime_error("missing companion trace event " + std::to_string(static_cast<unsigned>(event)));
}
void expectEqual(const std::string& actual, const std::string& expected, const std::string& what) {
    if (actual != expected)
        throw std::runtime_error(what + ": expected [" + expected + "], got [" + actual + "]");
}
CrabitWire::Message message(const char* topic, const std::string& value, uint16_t id) {
    CrabitWire::Message result{};
    const size_t topic_size = std::strlen(topic), value_size = value.size();
    require(topic_size <= CrabitWire::MAX_TOPIC && value_size <= CrabitWire::MAX_VALUE,
            "fixture message exceeds CrabitWire bounds");
    result.topic_len = static_cast<uint8_t>(topic_size);
    result.value_len = static_cast<uint8_t>(value_size);
    result.request_id = id;
    std::memcpy(result.topic, topic, topic_size + 1);
    std::memcpy(result.value, value.c_str(), value_size + 1);
    return result;
}
bool rx(const char* topic, const std::string& value, uint16_t id) {
    require(HostEventBus::network_handler != nullptr, "NetworkWeather::init did not register handler");
    return HostEventBus::network_handler(message(topic, value, id));
}
void app(const std::string& value, uint16_t id, const char* expected) {
    const size_t before = published.size();
    require(rx("app/net", value, id), "app/net was not consumed");
    require(published.size() == before + 1, "app/net should publish exactly one reply");
    expectEqual(published.back().topic, "app/net", "app/net reply topic");
    expectEqual(published.back().value, expected, "app/net reply value");
    require(published.back().request_id == id, "app/net reply lost its request id");
}
void expectTx(size_t index, const char* topic, const std::string& value, uint16_t id) {
    require(index < transmitted.size(), std::string("missing transmit ") + topic);
    const auto& item = transmitted[index];
    expectEqual(item.topic, topic, "transmit topic");
    expectEqual(item.value, value, "transmit payload for " + std::string(topic));
    require(item.request_id == id, std::string(topic) + " did not preserve session id");
}
void handshake(uint16_t id, const std::string& ssid, const std::string& password,
               const std::string& place) {
    const size_t first = transmitted.size();
    rx("net/status", "WIFI_MODE", id);
    expectEqual(status(), "CONFIGURING", "WIFI_MODE status");
    tick();
    expectTx(first, "net/ssid", ssid, id);
    rx("net/status", "WIFI_MODE", id); tick();
    require(transmitted.size() == first+1, "duplicate mode reply resent configuration");
    rx("net/status", "CFG_OK:ssid", id); tick();
    expectTx(first + 1, "net/pass", password, id);
    rx("net/status", "CFG_OK:ssid", id); tick();
    require(transmitted.size() == first+2, "late SSID ack advanced password stage");
    rx("net/status", "CFG_OK:pass", id); tick();
    expectTx(first + 2, "net/place", place, id);
    rx("net/status", "CFG_OK:place", id); tick();
    expectTx(first + 3, "net/run", "START", id);
    require(Crabit::NetworkWeather::busy(), "session stopped before weather run");
}
uint16_t startSync(const std::string& ssid = "LabNet", const std::string& password = "secret",
                   const std::string& place = "40712345,-73987654,0,3") {
    require(begin(), "configured state machine refused begin");
    const uint16_t id = session();
    handshake(id, ssid, password, place);
    return id;
}
void sendTime(uint16_t id, const char* epoch) {
    rx("net/status", "TIME_OK", id);
    rx("net/time", epoch, id);
}
void sendDay(uint16_t id, const std::string& tuple) { rx("net/day", tuple, id); }
void assertCachedBaseline() {
    require(count() == 3, "cached weather count changed");
    require(day(0).date == 20240229 && day(0).code == 99 && day(0).low == -1000 &&
            day(0).high == 1000 && day(0).rain == 100, "cached day zero changed");
    require(day(1).date == 20240301 && day(1).code == 0 && day(1).low == -1000 &&
            day(1).high == -1000 && day(1).rain == 0, "cached day one changed");
    require(day(2).date == 20240302 && day(2).code == 98 && day(2).low == -1 &&
            day(2).high == 1 && day(2).rain == 99, "cached day two changed");
}
void seedCache() {
    const uint16_t id = startSync();
    sendTime(id, "1709164800");
    sendDay(id, "0,20240229,99,-1000,1000,100");
    sendDay(id, "1,20240301,0,-1000,-1000,0");
    sendDay(id, "2,20240302,98,-1,1,99");
    require(count() == 0, "weather committed before DONE");
    rx("net/status", "DONE", id);
    expectEqual(status(), "DONE", "successful status");
    assertCachedBaseline();
}
void configureDefault(int offset = 0, int days = 3) {
    init();
    app("ssid=4c61624e6574", 10, "OK");
    app("pass=736563726574", 11, "OK");
    app("place=40712345,-73987654," + std::to_string(offset) + "," + std::to_string(days),
        12, "OK");
    require(Crabit::NetworkWeather::configured(), "valid config incomplete");
}
void expectClock(unsigned year, unsigned month, unsigned date, unsigned hour,
                 unsigned minute, unsigned second, unsigned weekday, const char* label) {
    require(last_rtc_time.Hours == hour && last_rtc_time.Minutes == minute &&
            last_rtc_time.Seconds == second, std::string(label) + " RTC time mismatch");
    require(last_rtc_date.Year == year % 100 && last_rtc_date.Month == month &&
            last_rtc_date.Date == date && last_rtc_date.WeekDay == weekday,
            std::string(label) + " RTC date/weekday mismatch");
    char expected[16]{};
    std::snprintf(expected, sizeof(expected), "%02u:%02u:%02u", hour, minute, second);
    expectEqual(GlobalStore.net_time, expected, std::string(label) + " display time");
}

void configCase() {
    init();
    require(!Crabit::NetworkWeather::configured(), "fresh config should be incomplete");
    const size_t before_zero_id = published.size();
    rx("app/net", "read", 0);
    require(published.size() == before_zero_id, "zero-id config request was answered");
    app("read", 20, "configured=0;days=3");

    app("ssid=4c61624e6574", 21, "OK");
    app("pass=736563726574", 22, "OK");
    app("place=-90000000,180000000,-720,1", 23, "OK");
    app("place=90000000,-180000000,840,7", 24, "OK");
    require(Crabit::NetworkWeather::configured(), "inclusive location bounds rejected");

    app("ssid=", 25, "INVALID");
    app("ssid=4c6", 26, "INVALID");
    app("ssid=4c00ab", 27, "INVALID");
    app("ssid=" + std::string(66, '5'), 28, "INVALID");
    app("pass=7", 29, "INVALID");
    app("pass=736563726574zz", 30, "INVALID");
    app("pass=" + std::string(130, '5'), 31, "INVALID");
    const std::vector<std::string> invalid_places = {
        "90000001,0,0,3", "-90000001,0,0,3", "0,180000001,0,3",
        "0,-180000001,0,3", "0,0,-721,3", "0,0,841,3", "0,0,0,0",
        "0,0,0,8", "0,0,0,3,4", "0,0,x,3"
    };
    uint16_t request = 32;
    for (const auto& place : invalid_places) app("place=" + place, request++, "INVALID");
    app("read", request++, "configured=1;days=7");

    require(begin(), "valid max-bound config did not begin");
    const uint16_t first_id = session();
    app("pass=6368616e676564", request++, "BUSY");
    handshake(first_id, "LabNet", "secret", "90000000,-180000000,840,7");
    cancel();
    expectEqual(status(), "CANCELLED", "cancel after handshake");

    app("pass=", request++, "OK");
    const uint16_t second_id = startSync("LabNet", "", "90000000,-180000000,840,7");
    require(second_id != first_id, "new session reused id");
    cancel();

    for (const auto& item : published) {
        if (item.topic == "app/net") {
            require(item.value.find("LabNet") == std::string::npos &&
                    item.value.find("secret") == std::string::npos &&
                    item.value.find("736563726574") == std::string::npos,
                    "configuration reply echoed credential material");
        }
    }
    for (const auto& line : usb_logs)
        require(line.find("LabNet") == std::string::npos && line.find("secret") == std::string::npos,
                "diagnostic log echoed credential material");
}

void successCase() {
    configureDefault();
    require(begin(), "begin failed");
    require(!begin(), "duplicate begin accepted while running");
    const uint16_t id = session();
    expectEqual(status(), "BOOT", "begin stage");
    handshake(id, "LabNet", "secret", "40712345,-73987654,0,3");
    sendTime(id, "1709210096");
    sendDay(id, "2,20240302,98,-1,1,99");
    sendDay(id, "0,20240229,99,-1000,1000,100");
    sendDay(id, "1,20240301,0,-1000,-1000,0");
    require(count() == 0 && day(0).date == 0, "weather snapshot visible before DONE");
    require(busy(), "session ended before DONE");
    rx("net/status", "DONE", id);
    expectEqual(status(), "DONE", "DONE stage");
    require(!busy(), "successful session remains busy");
    assertCachedBaseline();
    require(Crabit::NetworkWeather::timeValid(), "valid RTC update not recorded");
    expectClock(2024, 2, 29, 12, 34, 56, 4, "UTC");
    const auto& readback = traceAt(Crabit::CompanionTrace::TimeReadback);
    const auto& applied = traceAt(Crabit::CompanionTrace::TimeApplied);
    require(readback.session == id && readback.a == 20240229 && readback.b == 123456,
            "RTC readback trace must carry the actual date and time");
    require(applied.session == id && applied.a == 1709210096 && applied.b == 123456,
            "time-applied trace must carry requested epoch and local time");
    require(&readback < &applied, "RTC readback must be traced before time-applied");
}

void timeTraceCase() {
    configureDefault();

    uint16_t id = startSync();
    const size_t valid_start = trace_records.size();
    sendTime(id, "1709210096");
    require(trace_records.size() >= valid_start + 3, "valid time trace records missing");
    const auto& handled = traceAt(Crabit::CompanionTrace::TimeHandled);
    require(handled.session == id && handled.a == 5 && handled.b == ((uint32_t(id) << 1) | 1u),
            "handled trace must capture active session and weather step");
    require(traceAt(Crabit::CompanionTrace::TimeReadback).a == 20240229 &&
            traceAt(Crabit::CompanionTrace::TimeReadback).b == 123456,
            "actual HAL readback date/time mismatch");
    require(traceAt(Crabit::CompanionTrace::TimeApplied).a == 1709210096 &&
            traceAt(Crabit::CompanionTrace::TimeApplied).b == 123456,
            "requested time trace mismatch");
    for (const char* tuple : {"0,20240229,1,0,1,0", "1,20240301,1,0,1,0", "2,20240302,1,0,1,0"})
        sendDay(id, tuple);
    rx("net/status", "DONE", id);
    const auto& summary = traceAt(Crabit::CompanionTrace::TimeResult);
    require(summary.session == id && summary.a == 1 && summary.b == 7,
            "successful result trace must report time success and day bitmap");

    id = startSync();
    const size_t malformed_start = trace_records.size();
    rx("net/time", "", id);
    const auto& malformed_handled = trace_records[malformed_start];
    const auto& malformed_rejected = trace_records[malformed_start + 1];
    require(malformed_handled.event == Crabit::CompanionTrace::TimeHandled &&
            malformed_rejected.event == Crabit::CompanionTrace::TimeRejected &&
            malformed_rejected.session == id && malformed_rejected.a == 1,
            "empty/malformed time must be traced with rejection reason 1");
    cancel();

    const size_t stale_start = trace_records.size();
    rx("net/time", "1709210096", static_cast<uint16_t>(id - 1));
    require(trace_records[stale_start].event == Crabit::CompanionTrace::TimeHandled &&
            trace_records[stale_start + 1].event == Crabit::CompanionTrace::TimeRejected &&
            trace_records[stale_start + 1].session == static_cast<uint16_t>(id - 1) &&
            trace_records[stale_start + 1].a == 6 && trace_records[stale_start + 1].b == id,
            "stale time must be traced with rejection reason 6 and current session");
    cancel();

    require(begin(), "wrong-step session did not begin");
    id = session();
    const size_t wrong_step_start = trace_records.size();
    rx("net/time", "1709210096", id);
    require(trace_records[wrong_step_start].event == Crabit::CompanionTrace::TimeHandled &&
            trace_records[wrong_step_start].a == 0 &&
            trace_records[wrong_step_start + 1].event == Crabit::CompanionTrace::TimeRejected &&
            trace_records[wrong_step_start + 1].a == 7 && trace_records[wrong_step_start + 1].b == 0,
            "wrong-step time must be traced with rejection reason 7");
    cancel();

    id = startSync();
    rx("net/status", "TIME_OK", id);
    rtc_time_result = HAL_ERROR;
    const size_t hal_start = trace_records.size();
    rx("net/time", "1709210096", id);
    rtc_time_result = HAL_OK;
    require(trace_records[hal_start].event == Crabit::CompanionTrace::TimeHandled &&
            trace_records[hal_start + 1].event == Crabit::CompanionTrace::TimeRejected &&
            trace_records[hal_start + 1].a == 4,
            "HAL write error must be traced with rejection reason 4");
    require(std::none_of(trace_records.begin() + hal_start, trace_records.end(), [](const auto& record) {
        return record.event == Crabit::CompanionTrace::TimeReadback ||
               record.event == Crabit::CompanionTrace::TimeApplied;
    }), "HAL write failure must not claim readback or applied time");
    cancel();

    id = startSync();
    rtc_get_time_result = HAL_ERROR;
    const size_t read_error_start = trace_records.size();
    rx("net/time", "1709210096", id);
    rtc_get_time_result = HAL_OK;
    require(trace_records[read_error_start].event == Crabit::CompanionTrace::TimeHandled &&
            trace_records[read_error_start + 1].event == Crabit::CompanionTrace::TimeRejected &&
            trace_records[read_error_start + 1].a == 5,
            "HAL readback error must be traced with rejection reason 5");
    require(std::none_of(trace_records.begin() + read_error_start, trace_records.end(), [](const auto& record) {
        return record.event == Crabit::CompanionTrace::TimeReadback ||
               record.event == Crabit::CompanionTrace::TimeApplied;
    }), "HAL readback failure must not claim readback or applied time");
    cancel();
}

void dayValidationCase() {
    configureDefault();
    seedCache();
    auto rejectedDay = [](const std::string& tuple, const std::string& reason) {
        const uint16_t id = startSync();
        sendTime(id, "1709164800");
        sendDay(id, tuple);
        expectEqual(status(), "PARSE_FAILED", reason);
        require(!busy(), reason + " left session active");
        assertCachedBaseline();
    };

    uint16_t id = startSync();
    sendTime(id, "1709164800");
    sendDay(id, "0,20240229,1,0,1,0");
    sendDay(id, "0,20240301,1,0,1,0");
    expectEqual(status(), "PARSE_FAILED", "duplicate day index");
    assertCachedBaseline();

    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"3,20240229,1,0,1,0", "index outside configured days"},
        {"0,20230228,1,0,1,0", "year below contract"},
        {"0,21000101,1,0,1,0", "year above contract"},
        {"0,20250229,1,0,1,0", "non-leap February 29"},
        {"0,20240230,1,0,1,0", "invalid leap-month day"},
        {"0,20240229,-1,0,1,0", "negative condition code"},
        {"0,20240229,100,0,1,0", "condition code over maximum"},
        {"0,20240229,1,-1001,0,0", "minimum below range"},
        {"0,20240229,1,0,1001,0", "maximum above range"},
        {"0,20240229,1,2,1,0", "low above high"},
        {"0,20240229,1,0,1,-1", "negative rain"},
        {"0,20240229,1,0,1,101", "rain above maximum"},
        {"0,20240229,1,0,1", "missing tuple field"},
        {"0,20240229,1,0,1,0,7", "extra tuple field"}
    };
    for (const auto& item : invalid) rejectedDay(item.first, item.second);
}

void retentionCase() {
    configureDefault();
    seedCache();

    uint16_t id = startSync();
    sendTime(id, "1709164800");
    sendDay(id, "0,20240229,1,0,1,0");
    sendDay(id, "1,20240301,1,0,1,0");
    rx("net/status", "DONE", id);
    expectEqual(status(), "INCOMPLETE", "partial response DONE");
    assertCachedBaseline();

    id = startSync();
    sendTime(id, "1709164800");
    rx("net/status", "HTTP_FAILED", id);
    expectEqual(status(), "HTTP_FAILED", "backend failure status");
    assertCachedBaseline();

    id = startSync();
    rx("net/status", "DONE", id);
    expectEqual(status(), "INCOMPLETE", "DONE without days or session time");
    assertCachedBaseline();

    id = startSync();
    sendTime(id, "1709164800");
    sendDay(id, "0,20240302,1,0,1,0");
    sendDay(id, "1,20240301,1,0,1,0");
    sendDay(id, "2,20240303,1,0,1,0");
    rx("net/status", "DONE", id);
    expectEqual(status(), "INCOMPLETE", "non-monotonic date order");
    assertCachedBaseline();
    app("place=0,0,0,3", 62, "OK");
    require(count() == 0, "old region forecast remained visible after location change");
}

void rtcCase() {
    configureDefault();
    seedCache();

    uint16_t id = startSync();
    rx("net/status", "TIME_OK", id);
    expectEqual(status(), "TIME_PENDING", "remote status must not claim local RTC success");
    sendTime(id, "1709210096"); // 2024-02-29 12:34:56 UTC
    expectEqual(status(), "TIME_OK", "received RTC time completes pending status");
    expectClock(2024, 2, 29, 12, 34, 56, 4, "UTC leap day");
    rx("net/progress", "HTTPS_CONNECT", id);
    expectEqual(status(), "HTTPS_CONNECT", "HTTPS progress");
    rx("net/progress", "HTTP_READ", static_cast<uint16_t>(id-1));
    expectEqual(status(), "HTTPS_CONNECT", "stale progress ignored");
    rx("net/progress", "HTTP_READ", id);
    expectEqual(status(), "HTTP_READ", "HTTP progress");
    for (const char* progress : {"HTTP_SEND", "HTTP_HEADERS", "HTTP_BODY", "WEATHER_CLOSE"}) {
        rx("net/progress", progress, id);
        expectEqual(status(), progress, "detailed HTTP progress");
    }
    const auto logCount = usb_logs.size();
    rx("net/detail", "http=429", id);
    rx("net/detail", "bytes=512;ms=12000;ok=0", id);
    require(usb_logs.size() == logCount+2, "numeric details must reach USB logs");
    rx("net/detail", "password=PRIVATE", id);
    rx("net/detail", "http=200 PRIVATE", id);
    require(usb_logs.size() == logCount+2, "unstructured details must not reach USB logs");
    rx("net/status", "HTTP_FAILED", id);
    expectEqual(status(), "HTTP_FAILED", "weather failure after UTC update");
    require(std::string(Crabit::NetworkWeather::label()).find("已校时") != std::string::npos,
            "partial success must remain visible");
    assertCachedBaseline();

    app("place=40712345,-73987654,180,3", 60, "OK");
    id = startSync("LabNet", "secret", "40712345,-73987654,180,3");
    sendTime(id, "1709242200"); // 2024-02-29 21:30:00 UTC +03:00
    expectClock(2024, 3, 1, 0, 30, 0, 5, "offset across leap day");
    rx("net/status", "HTTP_FAILED", id);
    assertCachedBaseline();

    app("place=40712345,-73987654,480,3", 61, "OK");
    id = startSync("LabNet", "secret", "40712345,-73987654,480,3");
    sendTime(id, "1704124800"); // 2024-01-01 16:00:00 UTC +08:00
    expectClock(2024, 1, 2, 0, 0, 0, 2, "positive offset weekday");
    rx("net/status", "HTTP_FAILED", id);
    require(Crabit::NetworkWeather::timeValid(), "valid RTC was coupled to weather success");
    require(rtc_time_calls == 4 && rtc_date_calls == 4, "RTC writes not performed per valid time");
    assertCachedBaseline();
}

void weatherOnlyCase() {
    configureDefault();
    uint16_t id = startSync();
    rx("net/status", "TIME_UNAVAILABLE", static_cast<uint16_t>(id-1));
    require(busy(), "stale time warning terminated new session");
    rx("net/status", "TIME_UNAVAILABLE", id);
    require(busy() && !Crabit::NetworkWeather::timeValid(), "NTP warning must not abort weather or set time");
    auto finishForecast = [](uint16_t current) {
        sendDay(current, "0,20240229,1,0,1,0");
        sendDay(current, "1,20240301,1,0,1,0");
        sendDay(current, "2,20240302,1,0,1,0");
        rx("net/status", "DONE", current);
    };
    finishForecast(id);
    require(!busy() && count() == 3, "forecast without time must commit complete snapshot");
    expectEqual(status(), "WEATHER_ONLY", "partial success must not claim RTC success");
    require(!Crabit::NetworkWeather::timeValid() && rtc_time_calls == 0, "failed NTP must not write RTC");

    id = startSync();
    rtc_time_result = HAL_ERROR;
    rx("net/time", "1709210096", id);
    rx("net/status", "TIME_OK", id);
    require(busy(), "RTC write error must not cancel forecast");
    expectEqual(status(), "TIME_UNAVAILABLE", "TIME_OK must not override rejected local RTC write");
    finishForecast(id);
    expectEqual(status(), "WEATHER_ONLY", "RTC error remains partial success");
    require(count() == 3 && !Crabit::NetworkWeather::timeValid(), "RTC failure must not mark time valid");
    rtc_time_result = HAL_OK;
    id = startSync();
    rx("net/time", "invalid", id);
    require(busy(), "malformed time must not cancel forecast");
    finishForecast(id);
    expectEqual(status(), "WEATHER_ONLY", "malformed time remains partial success");
}

void staleCancelCase() {
    configureDefault();
    require(begin(), "begin failed");
    const uint16_t old_id = session();
    rx("net/status", "WIFI_MODE", old_id);
    rx("net/status", "CFG_OK", static_cast<uint16_t>(old_id - 1));
    tick();
    expectTx(0, "net/ssid", "LabNet", old_id);
    cancel();
    expectEqual(status(), "CANCELLED", "cancel state");
    const size_t sent = transmitted.size();
    rx("net/status", "WIFI_MODE", old_id);
    rx("net/time", "1709164800", old_id);
    rx("net/day", "0,20240229,1,0,1,0", old_id);
    require(!busy() && !Crabit::NetworkWeather::timeValid() && count() == 0,
            "cancelled session accepted late data");
    tick();
    require(transmitted.size() == sent, "cancelled session transmitted queued config");

    require(begin(), "new session after cancel failed");
    const uint16_t new_id = session();
    require(new_id != old_id, "session id did not advance after cancel");
    rx("net/status", "WIFI_MODE", old_id);
    tick();
    require(transmitted.size() == sent, "stale session advanced the new handshake");
    rx("net/status", "WIFI_MODE", new_id);
    tick();
    expectTx(sent, "net/ssid", "LabNet", new_id);
}

void timeoutAndTransmitCase() {
    configureDefault();
    fake_tick = 0xfffffff0u;
    require(begin(), "begin near tick wrap failed");
    fake_tick += 74999u;
    tick();
    require(busy(), "timeout fired before 75000 ms across tick wrap");
    fake_tick += 1u;
    tick();
    expectEqual(status(), "TIMEOUT", "wrapped timeout");
    require(!busy(), "timed out session remains busy");

    require(begin(), "begin after timeout failed");
    const uint16_t id = session();
    rx("net/status", "WIFI_MODE", id);
    HostEventBus::reject_next_transmit = true;
    const size_t before = transmitted.size();
    tick();
    expectEqual(status(), "TX_FAILED", "rejected EventBus transmit");
    require(!busy() && transmitted.size() == before, "rejected transmit was reported as sent");
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "scenario argument required");
        const std::string scenario = argv[1];
        if (scenario == "config") configCase();
        else if (scenario == "success") successCase();
        else if (scenario == "time-trace") timeTraceCase();
        else if (scenario == "day-validation") dayValidationCase();
        else if (scenario == "retention") retentionCase();
        else if (scenario == "rtc") rtcCase();
        else if (scenario == "weather-only") weatherOnlyCase();
        else if (scenario == "stale-cancel") staleCancelCase();
        else if (scenario == "timeout-tx") timeoutAndTransmitCase();
        else throw std::runtime_error("unknown scenario: " + scenario);
        std::cout << scenario << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
"""


class NetworkWeatherHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = next(
            (p for p in (PREFERRED_COMPILER, shutil.which("g++")) if p and Path(p).is_file()), None
        )
        if compiler is None:
            raise RuntimeError("existing local g++ required")
        cls.temporary = tempfile.TemporaryDirectory(prefix="crabit-network-weather-host-")
        temp = Path(cls.temporary.name)
        inc = temp / "firmware/stm32/Core/Inc"
        src = temp / "firmware/stm32/Core/Src"
        shared = temp / "firmware/shared"
        nanoui = inc / "NanoUI"
        for directory in (inc, src, shared, nanoui):
            directory.mkdir(parents=True, exist_ok=True)

        production_files = {
            ROOT / "firmware/shared/weather_contract.h": shared / "weather_contract.h",
            ROOT / "firmware/shared/crabit_wire.h": shared / "crabit_wire.h",
            ROOT / "firmware/stm32/Core/Inc/network_weather.h": inc / "network_weather.h",
            ROOT / "firmware/stm32/Core/Src/network_weather.cpp": src / "network_weather.cpp",
        }
        for original, copied in production_files.items():
            shutil.copyfile(original, copied)
            if copied.read_bytes() != original.read_bytes():
                raise AssertionError(f"production source was altered while staging: {original}")

        (inc / "global_store.h").write_text(MOCK_GLOBAL_STORE, encoding="utf-8")
        (nanoui / "NanoUI.h").write_text(MOCK_NANOUI, encoding="utf-8")
        (inc / "event_bus.h").write_text(MOCK_EVENT_BUS, encoding="utf-8")
        (inc / "rtc.h").write_text(MOCK_RTC, encoding="utf-8")
        (inc / "companion_trace.h").write_text(MOCK_COMPANION_TRACE, encoding="utf-8")
        driver = temp / "network_weather_host.cpp"
        driver.write_text(HOST_DRIVER, encoding="utf-8")
        cls.binary = temp / "network_weather_host.exe"
        build = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-I",
                str(inc),
                str(src / "network_weather.cpp"),
                str(driver),
                "-o",
                str(cls.binary),
            ],
            text=True,
            capture_output=True,
        )
        if build.returncode != 0:
            raise RuntimeError("host fixture compile failed:\n" + build.stdout + build.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.binary), scenario], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(), f"{scenario} passed")

    def test_app_net_config_validation_atomicity_bounds_and_secret_redaction(self):
        self.run_scenario("config")

    def test_mode_wifi_handshake_session_correlation_and_commit_at_done(self):
        self.run_scenario("success")

    def test_time_trace_reports_readback_rejections_and_result_summary(self):
        self.run_scenario("time-trace")

    def test_repeated_and_invalid_day_tuples_preserve_cached_snapshot(self):
        self.run_scenario("day-validation")

    def test_partial_failure_missing_time_and_unordered_days_retain_cache(self):
        self.run_scenario("retention")

    def test_utc_offset_leap_weekday_and_rtc_weather_independence(self):
        self.run_scenario("rtc")

    def test_ntp_or_local_rtc_failure_still_commits_complete_weather_without_false_time_success(
        self,
    ):
        self.run_scenario("weather-only")

    def test_stale_session_and_post_cancel_messages_are_ignored(self):
        self.run_scenario("stale-cancel")

    def test_timeout_tick_wrap_and_transmit_rejection(self):
        self.run_scenario("timeout-tx")


if __name__ == "__main__":
    unittest.main()
