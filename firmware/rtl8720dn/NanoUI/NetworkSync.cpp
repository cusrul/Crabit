#include "NetworkSync.h"
#include "src/CrabitBuildProfile.h"

#if CRABIT_BW16_ENABLE_NETWORK_SYNC

#include <Arduino.h>
#include <FreeRTOS.h>
#include <queue.h>
#include <WiFi.h>
#include <WiFiSSLClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" int findNtpSocket(uint16_t port);

#include "AmebaTypes.h"
#include "Protocol.h"
#include "src/crabit_shared/weather_contract.h"

extern volatile BW16Mode current_mode;

namespace NetworkSync {
namespace {

constexpr UBaseType_t kInputDepth = 4;
constexpr UBaseType_t kOutputDepth = 16;
constexpr uint32_t kTxSpacingMs = 120;
constexpr uint32_t kWifiTimeoutMs = 30000;
constexpr uint32_t kHttpTimeoutMs = 12000;
constexpr uint8_t kNtpMaxAttempts = 3;
constexpr uint32_t kNtpRetryMs = 500;
constexpr size_t kMaxHttpBody = 4096;
constexpr size_t kMaxHttpHeaders = 2048;

// ISRG Root X1, the public Let’s Encrypt trust anchor. It is intentionally a
// verified root, not a disabled-verification mode or a leaf certificate.
// Source: https://letsencrypt.org/certs/isrgrootx1.pem
static char kRootCaPem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

enum InputKind : uint8_t { INPUT_INVALID, INPUT_SSID, INPUT_PASS, INPUT_PLACE,
                           INPUT_RUN, INPUT_CANCEL };
enum OutputKind : uint8_t { OUTPUT_STATUS, OUTPUT_TIME, OUTPUT_DAY };
enum StatusCode : uint8_t {
    STATUS_INVALID, STATUS_BUSY, STATUS_CFG_SSID, STATUS_CFG_PASS,
    STATUS_CFG_PLACE, STATUS_CONNECTING, STATUS_WIFI_ON, STATUS_TIME_OK,
    STATUS_DONE, STATUS_WIFI_FAILED, STATUS_TIME_FAILED, STATUS_HTTP_FAILED,
    STATUS_PARSE_FAILED, STATUS_TX_FAILED, STATUS_CANCELLED, STATUS_TIME_UNAVAILABLE
};
enum RunStage : uint8_t {
    RUN_IDLE, WAIT_CONNECTING_TX, WIFI_BEGIN, WIFI_WAIT, WAIT_WIFI_TX,
    NTP_REQUEST, NTP_RETRY_WAIT, WAIT_TIME_TX, WAIT_TIME_OK_TX, HTTP_REQUEST, SEND_DAY,
    WAIT_DAY_TX, WAIT_DONE_TX, WAIT_TERMINAL_TX
};

struct InputEvent { CrabitWire::Message message; };
struct OutputEvent {
    uint16_t requestId;
    uint32_t epoch;
    uint8_t kind;
    uint8_t code;
    uint8_t dayIndex;
};

QueueHandle_t inputQueue = NULL;
QueueHandle_t outputQueue = NULL;
bool queuesReady = false;
char ssid[33] = {0};
char password[65] = {0};
bool haveSsid = false, havePassword = false, havePlace = false;
CrabitWeather::Place place;
CrabitWeather::Day forecast[CrabitWeather::MaxDays];
uint8_t forecastCount = 0, nextDay = 0;
uint16_t sessionId = 0;
RunStage runStage = RUN_IDLE;
uint32_t wifiStartedAt = 0;
uint8_t ntpAttempts = 0;
uint32_t ntpRetryAt = 0;
bool ntpStarted = false;
bool hasTransmitted = false;
uint32_t lastTransmitAt = 0;
WiFiUDP ntpUdp;
NTPClient ntp(ntpUdp, "pool.ntp.org", 0, 60000UL);
char httpBody[kMaxHttpBody + 1];

size_t boundedLength(const char* text, size_t limit) {
    if (!text) return limit;
    size_t i = 0;
    while (i < limit && text[i]) ++i;
    return i;
}

bool sameText(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}

bool isTerminal(StatusCode code) {
    return code == STATUS_DONE || code == STATUS_WIFI_FAILED ||
           code == STATUS_TIME_FAILED || code == STATUS_HTTP_FAILED ||
           code == STATUS_PARSE_FAILED || code == STATUS_TX_FAILED ||
           code == STATUS_CANCELLED;
}

const char* statusText(StatusCode code) {
    switch (code) {
    case STATUS_INVALID: return "INVALID";
    case STATUS_BUSY: return "BUSY";
    case STATUS_CFG_SSID: return "CFG_OK:ssid";
    case STATUS_CFG_PASS: return "CFG_OK:pass";
    case STATUS_CFG_PLACE: return "CFG_OK:place";
    case STATUS_CONNECTING: return "CONNECTING";
    case STATUS_WIFI_ON: return "WIFI_ON";
    case STATUS_TIME_OK: return "TIME_OK";
    case STATUS_DONE: return "DONE";
    case STATUS_WIFI_FAILED: return "WIFI_FAILED";
    case STATUS_TIME_FAILED: return "TIME_FAILED";
    case STATUS_TIME_UNAVAILABLE: return "TIME_UNAVAILABLE";
    case STATUS_HTTP_FAILED: return "HTTP_FAILED";
    case STATUS_PARSE_FAILED: return "PARSE_FAILED";
    case STATUS_TX_FAILED: return "TX_FAILED";
    case STATUS_CANCELLED: return "CANCELLED";
    }
    return "INVALID";
}

bool queueOutput(const OutputEvent& event) {
    return outputQueue && xQueueSend(outputQueue, &event, 0) == pdTRUE;
}

bool queueStatus(uint16_t id, StatusCode code) {
    OutputEvent event = {};
    event.requestId = id;
    event.kind = OUTPUT_STATUS;
    event.code = code;
    return queueOutput(event);
}

void disconnectNetwork() {
    if (ntpStarted) { ntp.end(); ntpStarted = false; }
    WiFi.disconnect();
}

void resetRun(bool disconnect) {
    runStage = RUN_IDLE;
    sessionId = 0;
    forecastCount = nextDay = 0;
    if (disconnect) disconnectNetwork();
}

void failRun(StatusCode code) {
    if (runStage == RUN_IDLE) return;
    const uint16_t id = sessionId;
    disconnectNetwork();
    runStage = WAIT_TERMINAL_TX;
    xQueueReset(outputQueue);
    if (!queueStatus(id, code)) resetRun(false);
}

bool enqueueInput(const CrabitWire::Message& message) {
    InputEvent event = {};
    event.message = message;
    event.message.topic[event.message.topic_len] = '\0';
    event.message.value[event.message.value_len] = '\0';
    if (xQueueSend(inputQueue, &event, 0) == pdTRUE) return true;
    return queueStatus(message.request_id, STATUS_BUSY);
}

InputKind classify(const CrabitWire::Message& message) {
    if (sameText(message.topic, "net/ssid")) return INPUT_SSID;
    if (sameText(message.topic, "net/pass")) return INPUT_PASS;
    if (sameText(message.topic, "net/place")) return INPUT_PLACE;
    if (sameText(message.topic, "net/run")) return INPUT_RUN;
    if (sameText(message.topic, "net/cancel")) return INPUT_CANCEL;
    return INPUT_INVALID;
}

void processInput(const CrabitWire::Message& message) {
    const InputKind kind = classify(message);
    const size_t valueLength = message.value_len;
    if (kind == INPUT_CANCEL) {
        if (runStage != RUN_IDLE && message.request_id == sessionId &&
            sameText(message.value, "CANCEL")) {
            failRun(STATUS_CANCELLED);
        } else {
            queueStatus(message.request_id, STATUS_INVALID);
        }
        return;
    }
    if (kind == INPUT_RUN) {
        if (runStage != RUN_IDLE) {
            queueStatus(message.request_id, STATUS_BUSY);
        } else if (!sameText(message.value, "START") || !haveSsid ||
                   !havePassword || !havePlace) {
            queueStatus(message.request_id, STATUS_INVALID);
        } else {
            sessionId = message.request_id;
            nextDay = forecastCount = 0;
            ntpAttempts = 0;
            runStage = WAIT_CONNECTING_TX;
            if (!queueStatus(sessionId, STATUS_CONNECTING)) failRun(STATUS_TX_FAILED);
        }
        return;
    }
    if (kind == INPUT_SSID || kind == INPUT_PASS || kind == INPUT_PLACE) {
        if (runStage != RUN_IDLE) {
            queueStatus(message.request_id, STATUS_BUSY);
            return;
        }
        if (kind == INPUT_SSID) {
            if (!valueLength || valueLength > 32 ||
                boundedLength(message.value, sizeof(message.value)) != valueLength) {
                queueStatus(message.request_id, STATUS_INVALID);
                return;
            }
            memcpy(ssid, message.value, valueLength);
            ssid[valueLength] = '\0';
            haveSsid = true;
            if (!queueStatus(message.request_id, STATUS_CFG_SSID))
                queueStatus(message.request_id, STATUS_TX_FAILED);
            return;
        }
        if (kind == INPUT_PASS) {
            if (valueLength > 64 ||
                boundedLength(message.value, sizeof(message.value)) != valueLength) {
                queueStatus(message.request_id, STATUS_INVALID);
                return;
            }
            memcpy(password, message.value, valueLength);
            password[valueLength] = '\0';
            havePassword = true;
            if (!queueStatus(message.request_id, STATUS_CFG_PASS))
                queueStatus(message.request_id, STATUS_TX_FAILED);
            return;
        }
        char placeText[CrabitWire::MAX_VALUE + 1];
        if (boundedLength(message.value, sizeof(message.value)) != valueLength) {
            queueStatus(message.request_id, STATUS_INVALID);
            return;
        }
        memcpy(placeText, message.value, valueLength);
        placeText[valueLength] = '\0';
        CrabitWeather::Place parsed;
        if (!CrabitWeather::parsePlace(placeText, parsed)) {
            queueStatus(message.request_id, STATUS_INVALID);
            return;
        }
        place = parsed;
        havePlace = true;
        if (!queueStatus(message.request_id, STATUS_CFG_PLACE))
            queueStatus(message.request_id, STATUS_TX_FAILED);
        return;
    }
    queueStatus(message.request_id, STATUS_INVALID);
}

// Small bounded JSON scanner: it skips arbitrary JSON values without copying
// them and extracts only the five daily arrays needed by this prototype.
struct Span { const char* begin; const char* end; };

void skipSpace(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
}

bool scanString(const char*& p, Span* span) {
    if (*p != '"') return false;
    const char* start = ++p;
    while (*p) {
        if (*p == '"') {
            if (span) { span->begin = start; span->end = p; }
            ++p;
            return true;
        }
        if (*p == '\\') {
            ++p;
            if (!*p) return false;
        }
        ++p;
    }
    return false;
}

bool skipValue(const char*& p, unsigned depth = 0) {
    if (depth > 12) return false;
    skipSpace(p);
    if (*p == '"') return scanString(p, NULL);
    if (*p == '[') {
        ++p;
        skipSpace(p);
        if (*p == ']') { ++p; return true; }
        for (;;) {
            if (!skipValue(p, depth + 1)) return false;
            skipSpace(p);
            if (*p == ']') { ++p; return true; }
            if (*p++ != ',') return false;
            skipSpace(p);
        }
    }
    if (*p == '{') {
        ++p;
        skipSpace(p);
        if (*p == '}') { ++p; return true; }
        for (;;) {
            if (!scanString(p, NULL)) return false;
            skipSpace(p);
            if (*p++ != ':') return false;
            if (!skipValue(p, depth + 1)) return false;
            skipSpace(p);
            if (*p == '}') { ++p; return true; }
            if (*p++ != ',') return false;
            skipSpace(p);
        }
    }
    const char* start = p;
    while (*p && *p != ',' && *p != ']' && *p != '}' &&
           *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
    return p != start;
}

bool objectField(Span object, const char* name, Span& result) {
    const char* p = object.begin;
    if (!p || *p != '{') return false;
    ++p;
    skipSpace(p);
    while (*p && *p != '}') {
        Span key;
        if (!scanString(p, &key)) return false;
        skipSpace(p);
        if (*p++ != ':') return false;
        skipSpace(p);
        Span value = {p, p};
        const char* valueStart = p;
        if (!skipValue(p)) return false;
        value.begin = valueStart;
        value.end = p;
        if (size_t(key.end - key.begin) == strlen(name) &&
            memcmp(key.begin, name, size_t(key.end - key.begin)) == 0) {
            result = value;
            return true;
        }
        skipSpace(p);
        if (*p == '}') break;
        if (*p++ != ',') return false;
        skipSpace(p);
    }
    return false;
}

bool arrayItem(Span array, unsigned wanted, Span& result) {
    const char* p = array.begin;
    if (!p || *p != '[') return false;
    ++p;
    skipSpace(p);
    for (unsigned i = 0; *p && *p != ']'; ++i) {
        const char* start = p;
        if (!skipValue(p)) return false;
        if (i == wanted) { result.begin = start; result.end = p; return true; }
        skipSpace(p);
        if (*p == ']') break;
        if (*p++ != ',') return false;
        skipSpace(p);
    }
    return false;
}

bool spanNumber(Span span, double& value) {
    char text[40];
    const size_t n = size_t(span.end - span.begin);
    if (!n || n >= sizeof(text) || (n == 4 && memcmp(span.begin, "null", 4) == 0)) return false;
    memcpy(text, span.begin, n);
    text[n] = '\0';
    char* end = NULL;
    value = strtod(text, &end);
    return end == text + n && isfinite(value);
}

bool spanDate(Span span, uint32_t& date) {
    if (span.end - span.begin != 12 || *span.begin != '"' || span.end[-1] != '"' ||
        span.begin[5] != '-' || span.begin[8] != '-') return false;
    uint32_t y = 0, m = 0, d = 0;
    for (int i = 1; i <= 4; ++i) {
        if (span.begin[i] < '0' || span.begin[i] > '9') return false;
        y = y * 10 + uint32_t(span.begin[i] - '0');
    }
    for (int i = 6; i <= 7; ++i) {
        if (span.begin[i] < '0' || span.begin[i] > '9') return false;
        m = m * 10 + uint32_t(span.begin[i] - '0');
    }
    for (int i = 9; i <= 10; ++i) {
        if (span.begin[i] < '0' || span.begin[i] > '9') return false;
        d = d * 10 + uint32_t(span.begin[i] - '0');
    }
    date = y * 10000 + m * 100 + d;
    return CrabitWeather::validDate(date);
}

bool parseForecast(char* body, CrabitWeather::Day* days, uint8_t count) {
    const char* p = body;
    skipSpace(p);
    Span root = {p, p + strlen(p)}, daily, times, codes, lows, highs, rains;
    if (!objectField(root, "daily", daily) || !objectField(daily, "time", times) ||
        !objectField(daily, "weather_code", codes) ||
        !objectField(daily, "temperature_2m_min", lows) ||
        !objectField(daily, "temperature_2m_max", highs) ||
        !objectField(daily, "precipitation_probability_max", rains)) return false;
    for (uint8_t i = 0; i < count; ++i) {
        Span dateSpan, codeSpan, lowSpan, highSpan, rainSpan;
        uint32_t date;
        double code, low, high, rain;
        if (!arrayItem(times, i, dateSpan) || !spanDate(dateSpan, date) ||
            !arrayItem(codes, i, codeSpan) || !spanNumber(codeSpan, code) ||
            !arrayItem(lows, i, lowSpan) || !spanNumber(lowSpan, low) ||
            !arrayItem(highs, i, highSpan) || !spanNumber(highSpan, high) ||
            !arrayItem(rains, i, rainSpan) || !spanNumber(rainSpan, rain)) return false;
        const long codeInt = lround(code);
        const long low10 = lround(low * 10.0);
        const long high10 = lround(high * 10.0);
        const long rainInt = lround(rain);
        if (fabs(code - codeInt) > 0.0001 || fabs(rain - rainInt) > 0.0001 ||
            codeInt < 0 || codeInt > 99 || rainInt < 0 || rainInt > 100 ||
            low10 < -1000 || low10 > 1000 || high10 < -1000 || high10 > 1000 ||
            low10 > high10) return false;
        if (i && date <= days[i-1].date) return false;
        days[i].date = date;
        days[i].code = uint16_t(codeInt);
        days[i].low = int16_t(low10);
        days[i].high = int16_t(high10);
        days[i].rain = uint8_t(rainInt);
        char check[64];
        const int written = snprintf(check, sizeof(check), "%u,%lu,%u,%d,%d,%u",
            unsigned(i), (unsigned long)days[i].date, unsigned(days[i].code),
            int(days[i].low), int(days[i].high), unsigned(days[i].rain));
        unsigned index;
        CrabitWeather::Day validated;
        if (written <= 0 || size_t(written) >= sizeof(check) ||
            !CrabitWeather::parseDay(check, index, validated) || index != i) return false;
    }
    return true;
}

bool readByte(WiFiSSLClient& client, uint32_t started, uint32_t limit, int& byteOut) {
    while (uint32_t(millis() - started) <= limit) {
        if (client.available() > 0) {
            byteOut = client.read();
            return byteOut >= 0;
        }
        if (!client.connected()) return false;
        delay(1);
    }
    return false;
}

bool readLine(WiFiSSLClient& client, uint32_t started, uint32_t timeout,
              char* line, size_t capacity, size_t& totalBytes) {
    size_t used = 0;
    bool sawCr = false;
    for (;;) {
        int c;
        if (!readByte(client, started, timeout, c)) return false;
        if (++totalBytes > kMaxHttpHeaders) return false;
        if (sawCr) {
            if (c != '\n') return false;
            line[used] = '\0';
            return true;
        }
        if (c == '\r') { sawCr = true; continue; }
        if (c == '\n' || used + 1 >= capacity) return false;
        line[used++] = char(c);
    }
}

bool readExact(WiFiSSLClient& client, uint32_t started, uint32_t timeout,
               char* out, size_t bytes, size_t& total) {
    for (size_t i = 0; i < bytes; ++i) {
        int c;
        if (!readByte(client, started, timeout, c) || total >= kMaxHttpBody) return false;
        out[i] = char(c);
        ++total;
    }
    return true;
}

bool readHttpBody(WiFiSSLClient& client, uint32_t started, bool chunked,
                  bool hasLength, size_t contentLength, size_t headerBytes,
                  char* body, size_t& bodyLength) {
    bodyLength = 0;
    if (chunked) {
        char line[64];
        size_t overhead = headerBytes;
        for (;;) {
            if (!readLine(client, started, kHttpTimeoutMs, line, sizeof(line), overhead)) return false;
            if (!((line[0] >= '0' && line[0] <= '9') || (line[0] >= 'a' && line[0] <= 'f') || (line[0] >= 'A' && line[0] <= 'F'))) return false;
            char* end = NULL;
            const unsigned long chunk = strtoul(line, &end, 16);
            if (end == line || chunk > kMaxHttpBody - bodyLength) return false;
            if (*end && *end != ';') return false;
            if (*end == ';') {
                if (!*++end) return false;
                while (*end >= ' ' && *end <= '~') ++end;
            }
            if (*end) return false;
            if (!chunk) {
                do {
                    if (!readLine(client, started, kHttpTimeoutMs, line, sizeof(line), overhead)) return false;
                } while (line[0]);
                break;
            }
            if (!readExact(client, started, kHttpTimeoutMs, body + bodyLength, size_t(chunk), bodyLength)) return false;
            int cr, lf;
            if (!readByte(client, started, kHttpTimeoutMs, cr) ||
                !readByte(client, started, kHttpTimeoutMs, lf) || cr != '\r' || lf != '\n') return false;
            overhead += 2;
            if (overhead > kMaxHttpHeaders) return false;
        }
    } else if (hasLength) {
        if (contentLength > kMaxHttpBody ||
            !readExact(client, started, kHttpTimeoutMs, body, contentLength, bodyLength)) return false;
    } else {
        while (bodyLength < kMaxHttpBody) {
            if (client.available() > 0) {
                const int c = client.read();
                if (c < 0) break;
                body[bodyLength++] = char(c);
            } else if (!client.connected()) break;
            else if (uint32_t(millis() - started) > kHttpTimeoutMs) return false;
            else delay(1);
        }
        if (client.connected() && bodyLength == kMaxHttpBody) return false;
    }
    body[bodyLength] = '\0';
    return bodyLength != 0;
}

void coordinateText(int32_t value, char* out, size_t cap) {
    const bool negative = value < 0;
    uint32_t magnitude = negative ? uint32_t(-int64_t(value)) : uint32_t(value);
    snprintf(out, cap, "%s%lu.%06lu", negative ? "-" : "",
             (unsigned long)(magnitude / 1000000UL),
             (unsigned long)(magnitude % 1000000UL));
    size_t n = strlen(out);
    while (n && out[n - 1] == '0') out[--n] = '\0';
    if (n && out[n - 1] == '.') out[n - 1] = '\0';
}

enum FetchResult : uint8_t { FETCH_OK, FETCH_HTTP_FAILED, FETCH_PARSE_FAILED };

FetchResult fetchForecast(CrabitWeather::Day* days, uint8_t count) {
    char latitude[20], longitude[20], path[320], request[640];
    coordinateText(place.latitude, latitude, sizeof(latitude));
    coordinateText(place.longitude, longitude, sizeof(longitude));
    const int pathLen = snprintf(path, sizeof(path),
        "/v1/forecast?latitude=%s&longitude=%s&daily=weather_code,temperature_2m_min,temperature_2m_max,precipitation_probability_max&forecast_days=%u&timezone=auto",
        latitude, longitude, unsigned(count));
    if (pathLen <= 0 || size_t(pathLen) >= sizeof(path)) return FETCH_HTTP_FAILED;
    const int requestLen = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: api.open-meteo.com\r\nUser-Agent: Crabit-BW16/1\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
        path);
    if (requestLen <= 0 || size_t(requestLen) >= sizeof(request)) return FETCH_HTTP_FAILED;

    static WiFiSSLClient client;
    client.setRootCA(reinterpret_cast<unsigned char*>(kRootCaPem));
    client.setRecvTimeout(1000);
    // The Realtek SDK's connect() is synchronous and exposes no connect deadline.
    // Send before entering blocking SDK code; queued output cannot drain here.
    if (!Protocol::send_message_checked("net/progress", "HTTPS_CONNECT", sessionId)) return FETCH_HTTP_FAILED;
    if (!client.connect("api.open-meteo.com", 443)) { client.stop(); return FETCH_HTTP_FAILED; }
    if (!Protocol::send_message_checked("net/progress", "HTTP_SEND", sessionId)) {
        client.stop(); return FETCH_HTTP_FAILED;
    }
    if (client.write(reinterpret_cast<const uint8_t*>(request), size_t(requestLen)) != size_t(requestLen)) {
        client.stop(); return FETCH_HTTP_FAILED;
    }
    if (!Protocol::send_message_checked("net/progress", "HTTP_READ", sessionId)) {
        client.stop(); return FETCH_HTTP_FAILED;
    }
    const uint32_t started = millis();
    size_t headerBytes = 0;
    char line[256];
    if (!readLine(client, started, kHttpTimeoutMs, line, sizeof(line), headerBytes)) {
        client.stop(); return FETCH_HTTP_FAILED;
    }
    if (strlen(line) < 12 || strncmp(line, "HTTP/1.", 7) != 0 || line[8] != ' ' ||
        line[9] != '2' || line[10] != '0' || line[11] != '0') {
        // Only report numeric status, never arbitrary server text.
        char detail[40];
        const unsigned code = strlen(line) >= 12 && line[9] >= '0' && line[9] <= '9' &&
            line[10] >= '0' && line[10] <= '9' && line[11] >= '0' && line[11] <= '9'
            ? unsigned(line[9]-'0')*100 + unsigned(line[10]-'0')*10 + unsigned(line[11]-'0') : 0;
        snprintf(detail, sizeof(detail), "http=%u", code);
        Protocol::send_message_checked("net/detail", detail, sessionId);
        client.stop(); return FETCH_HTTP_FAILED;
    }
    if (!Protocol::send_message_checked("net/progress", "HTTP_HEADERS", sessionId)) {
        client.stop(); return FETCH_HTTP_FAILED;
    }
    bool hasLength = false, chunked = false;
    size_t contentLength = 0;
    for (;;) {
        if (!readLine(client, started, kHttpTimeoutMs, line, sizeof(line), headerBytes)) {
            client.stop(); return FETCH_HTTP_FAILED;
        }
        if (!line[0]) break;
        char* colon = strchr(line, ':');
        if (!colon) { client.stop(); return FETCH_HTTP_FAILED; }
        *colon++ = '\0';
        while (*colon == ' ' || *colon == '\t') ++colon;
        for (char* c = line; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
        if (strcmp(line, "content-length") == 0) {
            char* end = NULL;
            const unsigned long n = strtoul(colon, &end, 10);
            if (end == colon || *end || n > kMaxHttpBody) { client.stop(); return FETCH_HTTP_FAILED; }
            contentLength = size_t(n);
            hasLength = true;
        } else if (strcmp(line, "transfer-encoding") == 0) {
            for (char* c = colon; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
            if (strstr(colon, "chunked")) chunked = true;
        } else if (strcmp(line, "content-encoding") == 0 && strcmp(colon, "identity") != 0) {
            client.stop(); return FETCH_HTTP_FAILED;
        }
    }
    size_t bodyLength = 0;
    if (!Protocol::send_message_checked("net/progress", "HTTP_BODY", sessionId)) {
        client.stop(); return FETCH_HTTP_FAILED;
    }
    const bool received = readHttpBody(client, started, chunked, hasLength,
        contentLength, headerBytes, httpBody, bodyLength);
    char detail[64];
    snprintf(detail, sizeof(detail), "bytes=%u;ms=%lu;ok=%u", unsigned(bodyLength),
             (unsigned long)(millis()-started), received ? 1u : 0u);
    Protocol::send_message_checked("net/detail", detail, sessionId);
    Protocol::send_message_checked("net/progress", "WEATHER_CLOSE", sessionId);
    client.stop();
    if (!received) return FETCH_HTTP_FAILED;
    if (!Protocol::send_message_checked("net/progress", "WEATHER_PARSE", sessionId)) return FETCH_HTTP_FAILED;
    return parseForecast(httpBody, days, count) ? FETCH_OK : FETCH_PARSE_FAILED;
}

void onOutputSent(const OutputEvent& event) {
    if (event.kind == OUTPUT_STATUS && event.code == STATUS_TX_FAILED) {
        if (runStage == WAIT_TERMINAL_TX && event.requestId == sessionId) resetRun(false);
        return;
    }
    if (runStage == RUN_IDLE || event.requestId != sessionId) return;
    if (event.kind == OUTPUT_STATUS) {
        const StatusCode code = StatusCode(event.code);
        if (code == STATUS_CONNECTING && runStage == WAIT_CONNECTING_TX) runStage = WIFI_BEGIN;
        else if (code == STATUS_WIFI_ON && runStage == WAIT_WIFI_TX) runStage = NTP_REQUEST;
        else if ((code == STATUS_TIME_OK || code == STATUS_TIME_UNAVAILABLE) &&
                 runStage == WAIT_TIME_OK_TX) runStage = HTTP_REQUEST;
        else if (code == STATUS_DONE && runStage == WAIT_DONE_TX) resetRun(true);
        else if (isTerminal(code) && runStage == WAIT_TERMINAL_TX) resetRun(false);
    } else if (event.kind == OUTPUT_TIME && runStage == WAIT_TIME_TX) {
        runStage = WAIT_TIME_OK_TX;
        if (!queueStatus(sessionId, STATUS_TIME_OK)) failRun(STATUS_TX_FAILED);
    } else if (event.kind == OUTPUT_DAY && runStage == WAIT_DAY_TX && event.dayIndex == nextDay) {
        ++nextDay;
        runStage = nextDay == forecastCount ? WAIT_DONE_TX : SEND_DAY;
        if (runStage == WAIT_DONE_TX && !queueStatus(sessionId, STATUS_DONE)) failRun(STATUS_TX_FAILED);
    }
}

void txFailure(const OutputEvent& failed) {
    xQueueReset(outputQueue);
    if (failed.kind == OUTPUT_STATUS && failed.code == STATUS_TX_FAILED) {
        if (runStage != RUN_IDLE) resetRun(true);
        return;
    }
    if (runStage != RUN_IDLE) failRun(STATUS_TX_FAILED);
    else queueStatus(failed.requestId, STATUS_TX_FAILED);
}

void sendNextOutput() {
    if (!outputQueue || uxQueueMessagesWaiting(outputQueue) == 0) return;
    const uint32_t now = millis();
    if (hasTransmitted && uint32_t(now - lastTransmitAt) < kTxSpacingMs) return;
    OutputEvent event;
    if (xQueueReceive(outputQueue, &event, 0) != pdTRUE) return;
    char value[CrabitWire::MAX_VALUE + 1];
    const char* topic = "net/status";
    if (event.kind == OUTPUT_STATUS) {
        snprintf(value, sizeof(value), "%s", statusText(StatusCode(event.code)));
    } else if (event.kind == OUTPUT_TIME) {
        topic = "net/time";
        snprintf(value, sizeof(value), "%lu", (unsigned long)event.epoch);
    } else {
        topic = "net/day";
        const CrabitWeather::Day& day = forecast[event.dayIndex];
        snprintf(value, sizeof(value), "%u,%lu,%u,%d,%d,%u",
            unsigned(event.dayIndex), (unsigned long)day.date, unsigned(day.code),
            int(day.low), int(day.high), unsigned(day.rain));
    }
    if (!Protocol::send_message_checked(topic, value, event.requestId)) {
        txFailure(event);
        return;
    }
    hasTransmitted = true;
    lastTransmitAt = millis();
    onOutputSent(event);
}

void runStep() {
    switch (runStage) {
    case WIFI_BEGIN:
        // This SDK call may block during association; its API has no async form.
        if (password[0]) WiFi.begin(ssid, password);
        else WiFi.begin(ssid);
        wifiStartedAt = millis();
        runStage = WIFI_WAIT;
        break;
    case WIFI_WAIT:
        if (WiFi.status() == WL_CONNECTED) {
            runStage = WAIT_WIFI_TX;
            if (!queueStatus(sessionId, STATUS_WIFI_ON)) failRun(STATUS_TX_FAILED);
        } else if (uint32_t(millis() - wifiStartedAt) >= kWifiTimeoutMs) {
            failRun(STATUS_WIFI_FAILED);
        }
        break;
    case NTP_REQUEST: {
        ++ntpAttempts;
        if (!Protocol::send_message_checked("net/progress", "NTP_REQUEST", sessionId)) {
            failRun(STATUS_TX_FAILED); break;
        }
        if (!ntpStarted) {
            ntp.begin();
            ntpStarted = true;
            char socketDetail[32];
            snprintf(socketDetail, sizeof(socketDetail), "ntp_socket=%d", findNtpSocket(NTP_DEFAULT_LOCAL_PORT));
            if (!Protocol::send_message_checked("net/detail", socketDetail, sessionId)) {
                failRun(STATUS_TX_FAILED); break;
            }
        }
        const bool updated = ntp.forceUpdate();
        const uint32_t epoch = uint32_t(ntp.getEpochTime());
        CrabitWeather::Civil civil;
        const bool valid = updated && CrabitWeather::civil(epoch, 0, civil);
        char detail[64];
        snprintf(detail, sizeof(detail), "ntp=%u;reason=%u;epoch=%lu", unsigned(ntpAttempts),
                 !updated ? 1u : valid ? 0u : 2u, (unsigned long)epoch);
        if (!Protocol::send_message_checked("net/detail", detail, sessionId)) {
            failRun(STATUS_TX_FAILED); break;
        }
        if (!valid) {
            if (ntpAttempts < kNtpMaxAttempts) {
                ntpRetryAt = millis();
                runStage = NTP_RETRY_WAIT;
            } else {
                // Time and forecast are independent results. Drain the warning
                // before HTTPS so STM32 does not treat it as a terminal error.
                runStage = WAIT_TIME_OK_TX;
                if (!queueStatus(sessionId, STATUS_TIME_UNAVAILABLE)) failRun(STATUS_TX_FAILED);
            }
            break;
        }
        OutputEvent event = {};
        event.requestId = sessionId;
        event.epoch = epoch;
        event.kind = OUTPUT_TIME;
        runStage = WAIT_TIME_TX;
        if (!queueOutput(event)) failRun(STATUS_TX_FAILED);
        break;
    }
    case NTP_RETRY_WAIT:
        if (uint32_t(millis()-ntpRetryAt) >= kNtpRetryMs) runStage = NTP_REQUEST;
        break;
    case HTTP_REQUEST: {
        // Match the example's persistent UDP lifetime across NTP retries;
        // release it only after that phase, before allocating the TLS socket.
        if (ntpStarted) { ntp.end(); ntpStarted = false; }
        const FetchResult result = fetchForecast(forecast, place.days);
        if (result != FETCH_OK) failRun(result == FETCH_PARSE_FAILED ? STATUS_PARSE_FAILED : STATUS_HTTP_FAILED);
        else {
            forecastCount = place.days;
            nextDay = 0;
            runStage = SEND_DAY;
        }
        break;
    }
    case SEND_DAY: {
        OutputEvent event = {};
        event.requestId = sessionId;
        event.kind = OUTPUT_DAY;
        event.dayIndex = nextDay;
        runStage = WAIT_DAY_TX;
        if (!queueOutput(event)) failRun(STATUS_TX_FAILED);
        break;
    }
    default: break;
    }
}

} // namespace

void init() {
    if (queuesReady) return;
    inputQueue = xQueueCreate(kInputDepth, sizeof(InputEvent));
    outputQueue = xQueueCreate(kOutputDepth, sizeof(OutputEvent));
    queuesReady = inputQueue && outputQueue;
    if (!queuesReady) {
        if (inputQueue) vQueueDelete(inputQueue);
        if (outputQueue) vQueueDelete(outputQueue);
        inputQueue = outputQueue = NULL;
    }
}

bool enqueue(const CrabitWire::Message& message) {
    if (!queuesReady || current_mode != MODE_WIFI || !message.request_id ||
        message.topic_len > CrabitWire::MAX_TOPIC || message.value_len > CrabitWire::MAX_VALUE ||
        boundedLength(message.topic, sizeof(message.topic)) != message.topic_len ||
        boundedLength(message.value, sizeof(message.value)) != message.value_len) return false;
    return enqueueInput(message);
}

void tick() {
    if (!queuesReady) return;
    if (current_mode != MODE_WIFI) {
        if (runStage != RUN_IDLE) resetRun(true);
        xQueueReset(inputQueue);
        xQueueReset(outputQueue);
        return;
    }
    InputEvent input;
    if (xQueueReceive(inputQueue, &input, 0) == pdTRUE) processInput(input.message);
    sendNextOutput();
    if (outputQueue && uxQueueMessagesWaiting(outputQueue)) return;
    runStep();
}

} // namespace NetworkSync

#else

namespace NetworkSync {
void init() {}
bool enqueue(const CrabitWire::Message&) { return false; }
void tick() {}
}

#endif
