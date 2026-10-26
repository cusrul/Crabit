"""Host tests that compile the actual BW16 weather parser/readers with a fake TLS client."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware" / "rtl8720dn" / "NanoUI" / "NetworkSync.cpp"
CONTRACT_DIR = ROOT / "firmware" / "shared"
PREFERRED_COMPILER = Path(r"C:\w64dev\w64devkit\bin\g++.exe")


def extract_braced(source: str, opening: int) -> str:
    """Return a C++ brace-delimited block, ignoring braces inside comments/literals."""
    if source[opening] != "{":
        raise ValueError("opening position is not a brace")
    depth = 0
    state = "code"
    i = opening
    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                state = "line-comment"
                i += 1
            elif ch == "/" and nxt == "*":
                state = "block-comment"
                i += 1
            elif ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return source[opening : i + 1]
        elif state == "line-comment":
            if ch == "\n":
                state = "code"
        elif state == "block-comment":
            if ch == "*" and nxt == "/":
                state = "code"
                i += 1
        elif state == "string":
            if ch == "\\":
                i += 1
            elif ch == '"':
                state = "code"
        elif state == "char":
            if ch == "\\":
                i += 1
            elif ch == "'":
                state = "code"
        i += 1
    raise ValueError("unclosed C++ brace block")


def extract_function(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise ValueError(f"production function not found: {signature}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise ValueError(f"function body not found: {signature}")
    return source[start:opening] + extract_braced(source, opening)


def extract_enum(source: str, declaration: str) -> str:
    start = source.find(declaration)
    if start < 0:
        raise ValueError(f"production enum not found: {declaration}")
    opening = source.find("{", start + len(declaration))
    return source[start:opening] + extract_braced(source, opening) + ";"


def production_constant(source: str, name: str) -> str:
    for line in source.splitlines():
        if name in line and line.lstrip().startswith("constexpr "):
            return line.strip()
    raise ValueError(f"production constant not found: {name}")


def make_harness(source: str) -> str:
    enum = extract_enum(source, "enum StatusCode : uint8_t")
    functions = [
        extract_function(source, "const char* statusText("),
        extract_function(source, "void skipSpace("),
        extract_function(source, "bool scanString("),
        extract_function(source, "bool skipValue("),
        extract_function(source, "bool objectField("),
        extract_function(source, "bool arrayItem("),
        extract_function(source, "bool spanNumber("),
        extract_function(source, "bool spanDate("),
        extract_function(source, "bool parseForecast("),
        extract_function(source, "bool readByte("),
        extract_function(source, "bool readLine("),
        extract_function(source, "bool readExact("),
        extract_function(source, "bool readHttpBody("),
    ]
    constants = "\n".join(
        production_constant(source, name)
        for name in ("kHttpTimeoutMs", "kMaxHttpBody", "kMaxHttpHeaders")
    )
    return (
        r"""#include <algorithm>
#include <math.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "weather_contract.h"

using std::size_t;
uint32_t fakeNow = 0;
uint32_t millis() { return fakeNow; }
void delay(uint32_t ms) { fakeNow += ms ? ms : 1; }

class WiFiSSLClient {
public:
    std::string input;
    size_t cursor = 0;
    bool holdOpen = false;
    mutable uint32_t availableDelay = 0;
    int available() const {
        fakeNow += availableDelay; availableDelay = 0;
        return int(input.size() - cursor);
    }
    int read() { return cursor < input.size() ? uint8_t(input[cursor++]) : -1; }
    bool connected() const { return holdOpen || cursor < input.size(); }
};

struct Span { const char* begin; const char* end; };
"""
        + constants
        + "\n"
        + enum
        + "\n"
        + "\n".join(functions)
        + r"""

#define REQUIRE(expr) do { if (!(expr)) { \
    std::cerr << "FAIL line " << __LINE__ << ": " #expr << "\n"; return 1; \
} } while (0)

std::string array(const std::vector<std::string>& items) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ',';
        out += items[i];
    }
    return out + "]";
}

std::string document(const std::string& dates, const std::string& codes,
                     const std::string& lows, const std::string& highs,
                     const std::string& rains) {
    return "{\"daily\":{\"time\":" + dates +
        ",\"weather_code\":" + codes +
        ",\"temperature_2m_min\":" + lows +
        ",\"temperature_2m_max\":" + highs +
        ",\"precipitation_probability_max\":" + rains + "}}";
}

bool parse(const std::string& json, unsigned count, CrabitWeather::Day* out) {
    std::vector<char> mutableBody(json.begin(), json.end());
    mutableBody.push_back('\0');
    return parseForecast(mutableBody.data(), out, uint8_t(count));
}

int forecastCases() {
    const char* dateValues[] = {"\"2026-03-01\"", "\"2026-03-02\"",
        "\"2026-03-03\"", "\"2026-03-04\"", "\"2026-03-05\"",
        "\"2026-03-06\"", "\"2026-03-07\""};
    for (unsigned n : {1u, 3u, 7u}) {
        std::vector<std::string> dates, codes, lows, highs, rains;
        for (unsigned i = 0; i < n; ++i) {
            dates.emplace_back(dateValues[i]);
            codes.emplace_back(std::to_string(i));
            lows.emplace_back(i == 0 ? "-1.5" : "-0.2");
            highs.emplace_back("3.0");
            rains.emplace_back(std::to_string(i * 10));
        }
        CrabitWeather::Day parsed[CrabitWeather::MaxDays] = {};
        REQUIRE(parse(document(array(dates), array(codes), array(lows), array(highs), array(rains)), n, parsed));
        REQUIRE(parsed[0].date == 20260301u);
        REQUIRE(parsed[0].low == -15 && parsed[0].high == 30);
        REQUIRE(parsed[n - 1].rain == (n - 1) * 10);
    }
    return 0;
}

int invalidCases() {
    const std::string d = "[\"2026-03-01\"]";
    const std::string c = "[0]", lo = "[-1.5]", hi = "[3]", r = "[0]";
    CrabitWeather::Day parsed[CrabitWeather::MaxDays] = {};
    REQUIRE(!parse("{not-json}", 1, parsed));
    REQUIRE(!parse(document(d, "[null]", lo, hi, r), 1, parsed));
    REQUIRE(!parse(document(d, c, "[null]", hi, r), 1, parsed));
    REQUIRE(!parse(document(d, c, lo, hi, "[null]"), 1, parsed));
    REQUIRE(!parse(document(d, "[-1]", lo, hi, r), 1, parsed));
    REQUIRE(!parse(document(d, "[100]", lo, hi, r), 1, parsed));
    REQUIRE(!parse(document(d, "[1.5]", lo, hi, r), 1, parsed));
    REQUIRE(!parse(document(d, c, lo, hi, "[-1]"), 1, parsed));
    REQUIRE(!parse(document(d, c, lo, hi, "[101]"), 1, parsed));
    REQUIRE(!parse(document(d, c, "[-100.1]", hi, r), 1, parsed));
    REQUIRE(!parse(document(d, c, lo, "[100.1]", r), 1, parsed));
    REQUIRE(!parse(document(d, c, "[4]", "[3]", r), 1, parsed));
    REQUIRE(!parse(document("[\"2026-02-30\"]", c, lo, hi, r), 1, parsed));
    REQUIRE(!parse(document("[\"2023-12-31\"]", c, lo, hi, r), 1, parsed));
    REQUIRE(!parse(document("[\"2026-3-01\"]", c, lo, hi, r), 1, parsed));
    REQUIRE(!parse(document(d, "[1e309]", lo, hi, r), 1, parsed));
    return 0;
}

int sortingCase() {
    CrabitWeather::Day parsed[CrabitWeather::MaxDays] = {};
    const std::string json = document("[\"2026-03-02\",\"2026-03-01\"]",
        "[1,0]", "[-1,-1]", "[1,1]", "[0,0]");
    REQUIRE(!parse(json, 2, parsed));
    return 0;
}

int readerCases() {
    char body[kMaxHttpBody + 1];
    size_t length = 0;

    WiFiSSLClient chunked;
    chunked.input = "4;ok=yes\r\nWiki\r\n5\r\npedia\r\n0\r\nX-Test: yes\r\n\r\n";
    fakeNow = 0;
    REQUIRE(readHttpBody(chunked, fakeNow, true, false, 0, 0, body, length));
    REQUIRE(length == 9 && std::string(body, length) == "Wikipedia");

    WiFiSSLClient contentLength;
    contentLength.input = "hello-extra";
    contentLength.holdOpen = true;
    fakeNow = 0;
    REQUIRE(readHttpBody(contentLength, fakeNow, false, true, 5, 0, body, length));
    REQUIRE(length == 5 && std::string(body, length) == "hello");

    WiFiSSLClient eofBody;
    eofBody.input = "close-delimited";
    fakeNow = 0;
    REQUIRE(readHttpBody(eofBody, fakeNow, false, false, 0, 0, body, length));
    REQUIRE(std::string(body, length) == "close-delimited");

    WiFiSSLClient shortBody;
    shortBody.input = "abc";
    fakeNow = 0;
    REQUIRE(!readHttpBody(shortBody, fakeNow, false, true, 4, 0, body, length));

    WiFiSSLClient overLimit;
    fakeNow = 0;
    REQUIRE(!readHttpBody(overLimit, fakeNow, false, true, kMaxHttpBody + 1, 0, body, length));

    WiFiSSLClient largeChunk;
    largeChunk.input = "1001\r\n";
    fakeNow = 0;
    REQUIRE(!readHttpBody(largeChunk, fakeNow, true, false, 0, 0, body, length));
    return 0;
}

int malformedChunkCase() {
    char body[kMaxHttpBody + 1];
    size_t length = 0;
    WiFiSSLClient malformed;
    malformed.input = "1g\r\na\r\n0\r\n\r\n";
    fakeNow = 0;
    REQUIRE(!readHttpBody(malformed, fakeNow, true, false, 0, 0, body, length));
    return 0;
}

int timeoutCase() {
    char line[32];
    size_t total = 0;
    int byte = 0;
    WiFiSSLClient stuck;
    stuck.holdOpen = true;
    fakeNow = 100;
    REQUIRE(!readByte(stuck, fakeNow, 5, byte));
    REQUIRE(fakeNow >= 106 && fakeNow <= 107);
    fakeNow = 0;
    REQUIRE(!readLine(stuck, fakeNow, 5, line, sizeof(line), total));
    REQUIRE(fakeNow >= 6 && fakeNow <= 7);
    // A synchronous SDK call can overrun the outer deadline before returning.
    // This injected stall demonstrates the limitation, not a device measurement.
    fakeNow = 0;
    stuck.availableDelay = 76000;
    REQUIRE(!readByte(stuck, 0, kHttpTimeoutMs, byte));
    REQUIRE(fakeNow >= 76000);
    return 0;
}

int statusCases() {
    REQUIRE(std::string(statusText(STATUS_CFG_SSID)) == "CFG_OK:ssid");
    REQUIRE(std::string(statusText(STATUS_CFG_PASS)) == "CFG_OK:pass");
    REQUIRE(std::string(statusText(STATUS_CFG_PLACE)) == "CFG_OK:place");
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string which = argv[1];
    int result = 2;
    if (which == "forecast") result = forecastCases();
    else if (which == "invalid") result = invalidCases();
    else if (which == "sorting") result = sortingCase();
    else if (which == "readers") result = readerCases();
    else if (which == "chunk-malformed") result = malformedChunkCase();
    else if (which == "timeout") result = timeoutCase();
    else if (which == "status") result = statusCases();
    if (result == 0) std::cout << "PASS " << which << "\n";
    return result;
}
"""
    )


class Bw16NetworkWeatherHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = SOURCE.read_text(encoding="utf-8")
        compiler = str(PREFERRED_COMPILER) if PREFERRED_COMPILER.is_file() else shutil.which("g++")
        if not compiler:
            raise RuntimeError("existing host g++ required; installing a compiler is out of scope")
        cls.temporary = tempfile.TemporaryDirectory(prefix="bw16-network-weather-")
        folder = Path(cls.temporary.name)
        cpp = folder / "network_weather_host.cpp"
        cls.executable = folder / "network_weather_host.exe"
        cpp.write_text(make_harness(source), encoding="utf-8")
        build = subprocess.run(
            [
                compiler,
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-I",
                str(CONTRACT_DIR),
                str(cpp),
                "-o",
                str(cls.executable),
            ],
            capture_output=True,
            text=True,
        )
        if build.returncode:
            cls.temporary.cleanup()
            raise AssertionError(build.stdout + build.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_case(self, name: str):
        result = subprocess.run([str(self.executable), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_forecast_parser_accepts_1_3_7_days_and_negative_temperatures(self):
        self.run_case("forecast")

    def test_actual_forecast_parser_rejects_bad_null_out_of_range_and_invalid_dates(self):
        self.run_case("invalid")

    def test_forecast_dates_must_be_ascending(self):
        self.run_case("sorting")

    def test_actual_http_readers_handle_chunks_length_eof_and_size_caps(self):
        self.run_case("readers")

    def test_http_reader_rejects_invalid_chunk_size_suffix(self):
        self.run_case("chunk-malformed")

    def test_actual_http_readers_enforce_deadlines(self):
        self.run_case("timeout")

    def test_actual_config_status_literals_are_typed(self):
        self.run_case("status")


if __name__ == "__main__":
    unittest.main(verbosity=2)
