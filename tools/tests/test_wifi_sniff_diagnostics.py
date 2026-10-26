"""Host execution tests for the complete BW16 Wi-Fi sniff diagnostics header."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "firmware/rtl8720dn/NanoUI/app/wifi_sniff_diagnostics.h"


HOST_DRIVER = r"""
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include "wifi_sniff_diagnostics.h"

std::mutex wifiSniffCriticalMutex;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool countsAreZero(const WifiSniffRxSnapshot& snapshot) {
    return snapshot.callbacks == 0 && snapshot.frames == 0 &&
           snapshot.beacons == 0 && snapshot.probeResponses == 0 &&
           snapshot.added == 0;
}

static void requireSameSnapshot(
    const WifiSniffRxSnapshot& actual,
    const WifiSniffRxSnapshot& expected,
    const char* message) {
    require(actual.active == expected.active &&
                actual.callbacks == expected.callbacks &&
                actual.frames == expected.frames &&
                actual.beacons == expected.beacons &&
                actual.probeResponses == expected.probeResponses &&
                actual.added == expected.added,
            message);
}

static void requireSameCounts(
    const WifiSniffRxSnapshot& actual,
    const WifiSniffRxSnapshot& expected,
    const char* message) {
    require(actual.callbacks == expected.callbacks &&
                actual.frames == expected.frames &&
                actual.beacons == expected.beacons &&
                actual.probeResponses == expected.probeResponses &&
                actual.added == expected.added,
            message);
}

static void requireConcurrentInvariants(
    const WifiSniffRxSnapshot& current,
    const WifiSniffRxSnapshot& previous,
    bool expectedActive) {
    require(current.active == expectedActive, "active state changed unexpectedly");
    require(current.callbacks >= current.frames, "frames exceeded callback count");
    require(current.beacons + current.probeResponses <= current.frames,
            "management subcounts exceeded frame count");
    require(current.added <= current.callbacks, "AP additions exceeded callbacks");
    require(current.callbacks >= previous.callbacks, "callback count decreased");
    require(current.frames >= previous.frames, "frame count decreased");
    require(current.beacons >= previous.beacons, "beacon count decreased");
    require(current.probeResponses >= previous.probeResponses,
            "probe-response count decreased");
    require(current.added >= previous.added, "AP addition count decreased");
}

static void lifecycleScenario() {
    WifiSniffRxSnapshot snapshot = wifiSniffRxSnapshot();
    require(!snapshot.active && countsAreZero(snapshot), "initial state is not empty");

    wifiSniffRxCallback();
    wifiSniffRxFrame(0x80);
    wifiSniffRxAccessPoint();
    snapshot = wifiSniffRxSnapshot();
    require(!snapshot.active && countsAreZero(snapshot), "inactive events were counted");

    wifiSniffRxBegin();
    snapshot = wifiSniffRxSnapshot();
    require(snapshot.active && countsAreZero(snapshot), "begin did not open empty state");

    wifiSniffRxCallback();
    wifiSniffRxCallback();
    wifiSniffRxFrame(0x80);
    wifiSniffRxAccessPoint();
    wifiSniffRxBegin();
    snapshot = wifiSniffRxSnapshot();
    require(snapshot.active && countsAreZero(snapshot), "begin did not reset active state");

    wifiSniffRxCallback();
    wifiSniffRxFrame(0x80);
    wifiSniffRxAccessPoint();
    const WifiSniffRxSnapshot beforeClose = wifiSniffRxSnapshot();
    require(beforeClose.active && beforeClose.callbacks == 1 &&
                beforeClose.frames == 1 && beforeClose.beacons == 1 &&
                beforeClose.probeResponses == 0 && beforeClose.added == 1,
            "active events were not recorded");

    wifiSniffRxEnd();
    const WifiSniffRxSnapshot closed = wifiSniffRxSnapshot();
    require(!closed.active, "end did not close diagnostics");
    requireSameCounts(closed, beforeClose, "end did not preserve counters");
    wifiSniffRxCallback();
    wifiSniffRxFrame(0x50);
    wifiSniffRxAccessPoint();
    requireSameSnapshot(wifiSniffRxSnapshot(), closed, "late events changed closed state");

    wifiSniffRxBegin();
    snapshot = wifiSniffRxSnapshot();
    require(snapshot.active && countsAreZero(snapshot), "restart did not reset and reopen");
    wifiSniffRxCallback();
    wifiSniffRxFrame(0x08);
    wifiSniffRxAccessPoint();
    snapshot = wifiSniffRxSnapshot();
    require(snapshot.active && snapshot.callbacks == 1 && snapshot.frames == 1 &&
                snapshot.beacons == 0 && snapshot.probeResponses == 0 &&
                snapshot.added == 1,
            "reopened diagnostics did not accept events");
}

static void classificationScenario() {
    wifiSniffRxBegin();

    for (int i = 0; i < 7; ++i) wifiSniffRxCallback();

    wifiSniffRxFrame(0x80);  // Beacon.
    wifiSniffRxFrame(0x50);  // Probe response.
    wifiSniffRxFrame(0x40);  // Probe request: frame only.
    wifiSniffRxFrame(0x08);  // Data: frame only.
    wifiSniffRxFrame(0xb4);  // Control: frame only.
    wifiSniffRxAccessPoint();
    wifiSniffRxAccessPoint();

    const WifiSniffRxSnapshot snapshot = wifiSniffRxSnapshot();
    require(snapshot.active, "classification scenario is inactive");
    require(snapshot.callbacks == 7 && snapshot.frames == 5,
            "callback and accepted-frame counters were not independent");
    require(snapshot.beacons == 1 && snapshot.probeResponses == 1,
            "management frame subcounts are incorrect");
    require(snapshot.added == 2, "AP additions did not count each add-only event");
}

static void concurrentScenario() {
    wifiSniffRxBegin();

    std::atomic<bool> closeReturned(false);
    std::atomic<bool> workerStarted(false);
    std::atomic<unsigned> completedIterations(0);
    const uint8_t frameTypes[] = {0x80, 0x50, 0x40, 0x08, 0xb4};

    std::thread producer([&]() {
        unsigned iteration = 0;
        workerStarted.store(true, std::memory_order_release);
        while (!closeReturned.load(std::memory_order_acquire)) {
            wifiSniffRxCallback();
            wifiSniffRxFrame(frameTypes[iteration % 5]);
            if (iteration % 3 == 0) wifiSniffRxAccessPoint();
            ++iteration;
            completedIterations.store(iteration, std::memory_order_release);
            if ((iteration & 15u) == 0) std::this_thread::yield();
        }

        // The worker deliberately calls all three APIs after End has returned.
        wifiSniffRxCallback();
        wifiSniffRxFrame(0x80);
        wifiSniffRxAccessPoint();
    });

    while (!workerStarted.load(std::memory_order_acquire)) std::this_thread::yield();

    WifiSniffRxSnapshot previous = wifiSniffRxSnapshot();
    while (completedIterations.load(std::memory_order_acquire) < 128) {
        const WifiSniffRxSnapshot current = wifiSniffRxSnapshot();
        requireConcurrentInvariants(current, previous, true);
        previous = current;
        std::this_thread::yield();
    }

    wifiSniffRxEnd();
    const WifiSniffRxSnapshot closed = wifiSniffRxSnapshot();
    requireConcurrentInvariants(closed, previous, false);
    require(closed.callbacks > 0 && closed.frames > 0,
            "concurrent producer did not publish events before close");

    closeReturned.store(true, std::memory_order_release);
    producer.join();

    const WifiSniffRxSnapshot afterLateEvents = wifiSniffRxSnapshot();
    requireSameSnapshot(afterLateEvents, closed,
                        "concurrent or late events changed the closed snapshot");
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "lifecycle") == 0) lifecycleScenario();
    else if (std::strcmp(argv[1], "classification") == 0) classificationScenario();
    else if (std::strcmp(argv[1], "concurrent") == 0) concurrentScenario();
    else return 2;
    return 0;
}
"""


class WifiSniffDiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("g++")
        if compiler is None:
            raise unittest.SkipTest("host g++ unavailable; no toolchain installation attempted")

        cls._temporary = tempfile.TemporaryDirectory(prefix="wifi-sniff-diagnostics-")
        fixture = Path(cls._temporary.name)
        mock_headers = fixture / "include"
        mock_headers.mkdir()
        (mock_headers / "FreeRTOS.h").write_text("#pragma once\n", encoding="utf-8")
        (mock_headers / "task.h").write_text(
            "#pragma once\n"
            "#include <mutex>\n"
            "extern std::mutex wifiSniffCriticalMutex;\n"
            "#define taskENTER_CRITICAL() wifiSniffCriticalMutex.lock()\n"
            "#define taskEXIT_CRITICAL() wifiSniffCriticalMutex.unlock()\n",
            encoding="utf-8",
        )
        driver = fixture / "wifi_sniff_diagnostics_host.cpp"
        driver.write_text(HOST_DRIVER, encoding="utf-8")
        cls._executable = fixture / "wifi_sniff_diagnostics_host.exe"

        result = subprocess.run(
            [
                compiler,
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pthread",
                "-I",
                str(mock_headers),
                "-I",
                str(HEADER.parent),
                str(driver),
                "-o",
                str(cls._executable),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            cls._temporary.cleanup()
            raise AssertionError(
                "host compilation of the production diagnostics header failed:\n"
                f"{result.stdout}{result.stderr}"
            )

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "_temporary"):
            cls._temporary.cleanup()

    def run_scenario(self, name: str) -> None:
        result = subprocess.run(
            [str(self._executable), name],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            f"scenario {name!r} failed:\n{result.stdout}{result.stderr}",
        )

    def test_inactive_begin_close_and_restart_lifecycle(self):
        self.run_scenario("lifecycle")

    def test_callback_frame_management_and_add_counters(self):
        self.run_scenario("classification")

    def test_threaded_snapshots_and_close_synchronization(self):
        self.run_scenario("concurrent")


if __name__ == "__main__":
    unittest.main()
