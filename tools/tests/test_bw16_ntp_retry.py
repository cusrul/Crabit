"""Exercise production BW16 NTP retry/state-machine bodies with host-only fakes."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/rtl8720dn/NanoUI/NetworkSync.cpp"
PROBE_SOURCE = ROOT / "firmware/rtl8720dn/NanoUI/NetworkSocketProbe.c"
CONTRACT_DIR = ROOT / "firmware/rtl8720dn/NanoUI/src/crabit_shared"
SHARED_DIR = ROOT / "firmware/shared"
PREFERRED_COMPILER = Path(r"C:\w64dev\w64devkit\bin\g++.exe")


def compiler() -> str | None:
    configured = os.environ.get("CXX")
    if configured:
        return shutil.which(configured) or (configured if Path(configured).is_file() else None)
    if PREFERRED_COMPILER.is_file():
        return str(PREFERRED_COMPILER)
    return shutil.which("g++") or shutil.which("x86_64-w64-mingw32-g++") or shutil.which("g++-14")


def extract_braced(source: str, opening: int) -> str:
    """Return a brace block, ignoring braces inside C++ comments and literals."""
    if source[opening] != "{":
        raise ValueError("opening position is not a brace")
    depth = 0
    state = "code"
    i = opening
    while i < len(source):
        char = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if char == "/" and nxt == "/":
                state = "line-comment"
                i += 1
            elif char == "/" and nxt == "*":
                state = "block-comment"
                i += 1
            elif char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[opening : i + 1]
        elif state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and nxt == "/":
                state = "code"
                i += 1
        elif state in ("string", "char"):
            if char == "\\":
                i += 1
            elif (state == "string" and char == '"') or (state == "char" and char == "'"):
                state = "code"
        i += 1
    raise ValueError("unclosed C++ brace block")


def extract_function(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise ValueError(f"production function not found: {signature}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise ValueError(f"production function body not found: {signature}")
    return source[start:opening] + extract_braced(source, opening)


def extract_declaration(source: str, declaration: str) -> str:
    start = source.find(declaration)
    if start < 0:
        raise ValueError(f"production declaration not found: {declaration}")
    opening = source.find("{", start + len(declaration))
    if opening < 0:
        raise ValueError(f"production declaration body not found: {declaration}")
    return source[start:opening] + extract_braced(source, opening) + ";"


def extract_constant(source: str, name: str) -> str:
    match = re.search(
        rf"^\s*constexpr\s+[^;\n]*\b{re.escape(name)}\s*=\s*[^;]+;", source, re.MULTILINE
    )
    if not match:
        raise ValueError(f"production constant not found: {name}")
    return match.group(0).strip()


def make_harness(source: str) -> str:
    declarations = "\n".join(
        extract_declaration(source, declaration)
        for declaration in (
            "enum InputKind : uint8_t",
            "enum OutputKind : uint8_t",
            "enum StatusCode : uint8_t",
            "enum RunStage : uint8_t",
            "struct OutputEvent",
        )
    )
    constants = "\n".join(
        extract_constant(source, name)
        for name in ("kWifiTimeoutMs", "kNtpMaxAttempts", "kNtpRetryMs")
    )
    functions = (
        extract_function(PROBE_SOURCE.read_text(encoding="utf-8"), "int findNtpSocket(")
        + "\n\n"
        + "\n\n".join(
            extract_function(source, signature)
            for signature in (
                "size_t boundedLength(",
                "bool sameText(",
                "bool isTerminal(",
                "bool queueOutput(",
                "bool queueStatus(",
                "void disconnectNetwork()",
                "void resetRun(",
                "void failRun(",
                "InputKind classify(",
                "void processInput(",
                "void onOutputSent(",
                "void runStep()",
            )
        )
    )
    return (
        r"""#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "weather_contract.h"
#include "crabit_wire.h"

using std::size_t;
uint32_t fakeNow = 0;
uint32_t millis() { return fakeNow; }

using socklen_t = unsigned;
struct sockaddr { };
struct sockaddr_in { uint16_t sin_family; uint16_t sin_port; };
constexpr int LWIP_SOCKET_OFFSET = 0;
constexpr int MEMP_NUM_NETCONN = 4;
constexpr int AF_INET = 2;
constexpr uint16_t NTP_DEFAULT_LOCAL_PORT = 1337;
struct FakeSocket { bool present = false; uint16_t family = 0; uint16_t port = 0; };
FakeSocket fakeSockets[MEMP_NUM_NETCONN];
uint16_t fakeNtohs(uint16_t value) { return uint16_t((value >> 8) | (value << 8)); }
#define ntohs(value) fakeNtohs(value)
int lwip_getsockname(int fd, sockaddr* address, socklen_t* length) {
    errno = EACCES;
    if (fd < 0 || fd >= MEMP_NUM_NETCONN || !fakeSockets[fd].present) return -1;
    auto* local = reinterpret_cast<sockaddr_in*>(address);
    local->sin_family = fakeSockets[fd].family;
    local->sin_port = fakeSockets[fd].port;
    *length = sizeof(sockaddr_in);
    return 0;
}

"""
        + constants
        + "\n"
        + declarations
        + r"""

enum FetchResult : uint8_t { FETCH_OK, FETCH_HTTP_FAILED, FETCH_PARSE_FAILED };
extern bool ntpStarted;
struct FetchStats { unsigned calls = 0; bool ntpClosedAtCall = false; } fetchStats;
FetchResult fetchForecast(CrabitWeather::Day*, uint8_t) {
    ++fetchStats.calls;
    fetchStats.ntpClosedAtCall = !ntpStarted;
    return FETCH_OK;
}

struct QueueHandle { } outputStorage;
using QueueHandle_t = QueueHandle*;
constexpr int pdTRUE = 1;
QueueHandle_t outputQueue = &outputStorage;
std::vector<OutputEvent> outputEvents;
int xQueueSend(QueueHandle_t queue, const void* data, unsigned) {
    if (queue != outputQueue) return 0;
    outputEvents.push_back(*static_cast<const OutputEvent*>(data));
    return pdTRUE;
}
void xQueueReset(QueueHandle_t queue) { if (queue == outputQueue) outputEvents.clear(); }

struct NtpResult { bool updated; uint32_t epoch; };
struct FakeNtp {
    std::vector<NtpResult> plan;
    size_t cursor = 0;
    NtpResult last{false, 0};
    unsigned beginCalls = 0, forceCalls = 0, endCalls = 0;
    void begin() { ++beginCalls; }
    bool forceUpdate() {
        ++forceCalls;
        last = cursor < plan.size() ? plan[cursor++] : NtpResult{false, 0};
        return last.updated;
    }
    uint32_t getEpochTime() const { return last.epoch; }
    void end() { ++endCalls; }
} ntp;

constexpr int WL_CONNECTED = 3;
struct FakeWiFi {
    unsigned disconnectCalls = 0, beginCalls = 0;
    void begin(const char*) { ++beginCalls; }
    void begin(const char*, const char*) { ++beginCalls; }
    int status() const { return WL_CONNECTED; }
    void disconnect() { ++disconnectCalls; }
} WiFi;

namespace Protocol {
struct Record { std::string topic, value; uint16_t id; };
std::vector<Record> records;
bool send_message_checked(const char* topic, const char* value, uint16_t id) {
    records.push_back({topic, value, id});
    return true;
}
}

char ssid[33] = "host-ap", password[65] = "host-pass";
bool haveSsid = true, havePassword = true, havePlace = true;
CrabitWeather::Place place{};
CrabitWeather::Day forecast[CrabitWeather::MaxDays]{};
uint8_t forecastCount = 0, nextDay = 0;
uint16_t sessionId = 0;
RunStage runStage = RUN_IDLE;
uint32_t wifiStartedAt = 0;
uint8_t ntpAttempts = 0;
uint32_t ntpRetryAt = 0;
bool ntpStarted = false;

"""
        + functions
        + r"""

#define REQUIRE(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

void clearRecords() { Protocol::records.clear(); }
const Protocol::Record* findRecord(const char* topic, const char* value = nullptr) {
    for (const auto& record : Protocol::records)
        if (record.topic == topic && (!value || record.value == value)) return &record;
    return nullptr;
}
unsigned recordsFor(const char* topic, const char* value = nullptr) {
    unsigned count = 0;
    for (const auto& record : Protocol::records)
        count += record.topic == topic && (!value || record.value == value);
    return count;
}
int socketDiagnosticCases() {
    for (auto& socket : fakeSockets) socket = {};
    errno = ERANGE;
    fakeSockets[0] = {true, AF_INET, uint16_t((NTP_DEFAULT_LOCAL_PORT << 8) |
                                              (NTP_DEFAULT_LOCAL_PORT >> 8))};
    if (findNtpSocket(NTP_DEFAULT_LOCAL_PORT) != 0 || errno != ERANGE) return 80;
    for (auto& socket : fakeSockets) socket = {};
    fakeSockets[2] = {true, AF_INET, uint16_t((NTP_DEFAULT_LOCAL_PORT << 8) |
                                              (NTP_DEFAULT_LOCAL_PORT >> 8))};
    errno = EDOM;
    if (findNtpSocket(NTP_DEFAULT_LOCAL_PORT) != 2 || errno != EDOM) return 81;
    for (auto& socket : fakeSockets) socket = {};
    errno = EBUSY;
    if (findNtpSocket(NTP_DEFAULT_LOCAL_PORT) != -1 || errno != EBUSY) return 82;
    return 0;
}
void resetHarness(uint16_t id, std::vector<NtpResult> plan) {
    fakeNow = 10000;
    runStage = NTP_REQUEST;
    sessionId = id;
    ntpAttempts = 0;
    ntpRetryAt = 0;
    ntp.plan = std::move(plan);
    ntp.cursor = ntp.forceCalls = ntp.beginCalls = ntp.endCalls = 0;
    ntpStarted = false;
    for (auto& socket : fakeSockets) socket = {};
    outputEvents.clear();
    clearRecords();
    fetchStats.calls = 0;
}
int successAfterRetry() {
    resetHarness(41, {{false, 0}, {true, 1770000000u}});
    runStep();
    if (runStage != NTP_RETRY_WAIT || ntpAttempts != 1 || ntp.forceCalls != 1) return 1;
    if (ntp.beginCalls != 1 || ntp.endCalls != 0 || !ntpStarted) return 66;
    if (recordsFor("net/detail", "ntp_socket=-1") != 1) return 83;
    if (!findRecord("net/progress", "NTP_REQUEST") ||
        !findRecord("net/detail", "ntp=1;reason=1;epoch=0")) return 2;
    const uint32_t firstRetryAt = ntpRetryAt;
    fakeNow = firstRetryAt + kNtpRetryMs - 1;
    runStep();
    if (runStage != NTP_RETRY_WAIT || ntp.forceCalls != 1) return 3;
    ++fakeNow;
    runStep(); // deadline releases the wait; the following scheduler step makes attempt 2
    if (runStage != NTP_REQUEST || ntp.forceCalls != 1) return 4;
    runStep();
    if (runStage != WAIT_TIME_TX || ntpAttempts != 2 || ntp.forceCalls != 2) return 5;
    if (ntp.beginCalls != 1 || ntp.endCalls != 0 || !ntpStarted) return 67;
    if (recordsFor("net/detail", "ntp_socket=-1") != 1) return 84;
    if (recordsFor("net/progress", "NTP_REQUEST") != 2 ||
        !findRecord("net/detail", "ntp=2;reason=0;epoch=1770000000")) return 6;
    if (outputEvents.size() != 1 || outputEvents[0].kind != OUTPUT_TIME ||
        outputEvents[0].requestId != 41 || outputEvents[0].epoch != 1770000000u) return 7;
    const OutputEvent time = outputEvents[0];
    outputEvents.clear();
    onOutputSent(time);
    if (runStage != WAIT_TIME_OK_TX || outputEvents.size() != 1 ||
        outputEvents[0].code != STATUS_TIME_OK) return 8;
    const OutputEvent ok = outputEvents[0];
    outputEvents.clear();
    onOutputSent(ok);
    if (runStage != HTTP_REQUEST) return 9;
    if (ntp.endCalls != 0 || !ntpStarted) return 68;
    runStep();
    if (ntp.endCalls != 1 || ntpStarted || fetchStats.calls != 1 ||
        !fetchStats.ntpClosedAtCall) return 69;
    return 0;
}

int exhaustionAndInvalidEpoch() {
    resetHarness(52, {{false, 0}, {true, 0}, {false, 0}});
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        if (runStage != NTP_REQUEST) return 10 + attempt;
        runStep();
        if (ntpAttempts != attempt || ntp.forceCalls != attempt) return 20 + attempt;
        if (ntp.beginCalls != 1 || ntp.endCalls != 0 || !ntpStarted) return 70 + attempt;
        if (recordsFor("net/detail", "ntp_socket=-1") != 1) return 85 + attempt;
        if (attempt < 3) {
            if (runStage != NTP_RETRY_WAIT) return 30 + attempt;
            fakeNow = ntpRetryAt + kNtpRetryMs;
            runStep();
            if (runStage != NTP_REQUEST) return 40 + attempt;
        }
    }
    if (runStage != WAIT_TIME_OK_TX || outputEvents.size() != 1 ||
        outputEvents[0].kind != OUTPUT_STATUS ||
        outputEvents[0].code != STATUS_TIME_UNAVAILABLE) return 50;
    if (recordsFor("net/progress", "NTP_REQUEST") != 3) return 51;
    if (!findRecord("net/detail", "ntp=1;reason=1;epoch=0") ||
        !findRecord("net/detail", "ntp=2;reason=2;epoch=0") ||
        !findRecord("net/detail", "ntp=3;reason=1;epoch=0")) return 52;
    for (const auto& event : outputEvents) if (event.kind == OUTPUT_TIME) return 53;
    const OutputEvent unavailable = outputEvents[0];
    outputEvents.clear();
    onOutputSent(unavailable);
    if (runStage != HTTP_REQUEST) return 54;
    if (ntp.endCalls != 0 || !ntpStarted) return 74;
    runStep();
    if (fetchStats.calls != 1 || runStage != SEND_DAY || ntp.endCalls != 1 || ntpStarted ||
        !fetchStats.ntpClosedAtCall) return 55;
    return 0;
}

CrabitWire::Message message(const char* topic, const char* value, uint16_t id) {
    CrabitWire::Message m{};
    m.topic_len = uint8_t(std::strlen(topic));
    m.value_len = uint8_t(std::strlen(value));
    m.request_id = id;
    std::memcpy(m.topic, topic, m.topic_len + 1);
    std::memcpy(m.value, value, m.value_len + 1);
    return m;
}

int inputResetCancelAndStaleOutput() {
    resetHarness(63, {{false, 0}});
    runStep();
    if (ntp.beginCalls != 1 || ntp.endCalls != 0 || !ntpStarted) return 75;
    runStage = RUN_IDLE;
    ntpAttempts = 2;
    processInput(message("net/run", "START", 64));
    if (runStage != WAIT_CONNECTING_TX || sessionId != 64 || ntpAttempts != 0) return 60;

    runStage = NTP_RETRY_WAIT;
    ntpAttempts = 1;
    ntpRetryAt = fakeNow;
    processInput(message("net/cancel", "CANCEL", 64));
    if (runStage != WAIT_TERMINAL_TX || outputEvents.size() != 1 ||
        outputEvents[0].code != STATUS_CANCELLED) return 61;
    OutputEvent staleUnavailable{};
    staleUnavailable.requestId = 64;
    staleUnavailable.kind = OUTPUT_STATUS;
    staleUnavailable.code = STATUS_TIME_UNAVAILABLE;
    onOutputSent(staleUnavailable);
    if (runStage != WAIT_TERMINAL_TX || fetchStats.calls != 0) return 62;
    const OutputEvent cancelled = outputEvents[0];
    outputEvents.clear();
    onOutputSent(cancelled);
    if (runStage != RUN_IDLE || sessionId != 0 || ntp.endCalls != 1 || ntpStarted) return 63;

    // A delayed result from the cancelled session cannot advance a new session.
    ntpAttempts = 2;
    processInput(message("net/run", "START", 65));
    if (runStage != WAIT_CONNECTING_TX || sessionId != 65 || ntpAttempts != 0) return 64;
    runStage = NTP_REQUEST;
    runStep();
    if (ntp.beginCalls != 2 || ntp.endCalls != 1 || !ntpStarted) return 76;
    runStage = WAIT_TIME_OK_TX;
    onOutputSent(staleUnavailable);
    if (runStage != WAIT_TIME_OK_TX || fetchStats.calls != 0) return 65;
    return 0;
}

int main() {
    const int socket = socketDiagnosticCases();
    if (socket) { std::fprintf(stderr, "socket diagnostic scenario: %d\n", socket); return socket; }
    const int success = successAfterRetry();
    if (success) { std::fprintf(stderr, "fail-then-success scenario: %d\n", success); return success; }
    const int exhausted = exhaustionAndInvalidEpoch();
    if (exhausted) { std::fprintf(stderr, "exhaustion scenario: %d\n", exhausted); return exhausted; }
    const int stale = inputResetCancelAndStaleOutput();
    if (stale) { std::fprintf(stderr, "cancel/reset scenario: %d\n", stale); return stale; }
    std::puts("bw16-ntp-retry: production runStep/onOutputSent/processInput passed retry, invalid epoch, exhaustion, success, and stale-output cases");
    return 0;
}
"""
    )


class Bw16NtpRetryTests(unittest.TestCase):
    def test_production_ntp_retry_state_machine(self) -> None:
        cxx = compiler()
        if not cxx:
            self.skipTest(
                "No existing g++/w64 compiler found; toolchain installation is out of scope"
            )
        source = SOURCE.read_text(encoding="utf-8")
        for marker in (
            "NTP_RETRY_WAIT",
            "STATUS_TIME_UNAVAILABLE",
            "ntp=",
            "ntpAttempts = 0;",
            "ntpStarted",
            "findNtpSocket",
        ):
            self.assertIn(
                marker, source, f"Lead's production NTP retry change has not landed: {marker}"
            )
        harness = make_harness(source)
        with tempfile.TemporaryDirectory(prefix="crabit-bw16-ntp-retry-") as temp:
            temp_path = Path(temp)
            fixture = temp_path / "ntp_retry_host.cpp"
            binary = temp_path / "ntp-retry-host.exe"
            fixture.write_text(harness, encoding="utf-8")
            command = [
                cxx,
                "-std=c++17",
                "-O0",
                "-g",
                "-I",
                str(CONTRACT_DIR),
                "-I",
                str(SHARED_DIR),
                str(fixture),
                "-o",
                str(binary),
            ]
            build = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout, end="")


if __name__ == "__main__":
    unittest.main(verbosity=2)
