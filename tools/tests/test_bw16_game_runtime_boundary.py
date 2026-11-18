"""BW16 game/wireless App runtime boundary, checked with the host toolchain.

Game profiles must reach the shared game runtime without pulling the
Wi-Fi/packet-injection/AuditRelay core into the same translation unit, while
every legacy wireless App keeps the same helpers. Hopper itself now runs on the
managed ArduboyCompatApp FRAME lifecycle and is checked through that boundary.

Evidence here is host-only: the real launcher preprocessed with ``-H`` (actual
include graph) plus real compile/run probes of the real headers against minimal
FreeRTOS fakes. No Arduino/STM32 build, SDK, device or flash operation is used.
"""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / "firmware/rtl8720dn/NanoUI"
GAMES = SKETCH / "app/apps/games"
LAUNCHER = SKETCH / "app/core/launcher.h"
WIFI_CORE = SKETCH / "app/core/wifi_hack_core.h"
RUNTIME = SKETCH / "app/core/app_runtime.h"

# Headers supplied by the Ameba/Arduino SDK or by board libraries. The host
# preprocessor only needs them to exist; they are never compiled here.
SDK_STUBS = (
    "Arduino.h", "WiFi.h", "WiFiUdp.h", "wifi_conf.h", "wifi_util.h",
    "OTA.h", "AmebaMDNS.h", "queue.h", "FreeRTOS.h", "task.h",
    "BLEDevice.h", "BLEAdvert.h", "BLEAdvertData.h", "BLEHIDDevice.h",
    "BLEHIDKeyboard.h", "BLEHIDMouse.h", "BLEUUID.h", "BLEScan.h",
    "BLEServer.h", "BLECharacteristic.h", "BW16BLESpam.h",
)

# Minimal fake runtime for compile probes: only what app_runtime.h needs.
RTOS_FAKE_ARDUINO = """\
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
using String = std::string;
void delayMicroseconds(unsigned int us);
"""

RTOS_FAKE_FREERTOS = """\
#pragma once
#include <cstdint>
using TickType_t = uint32_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
"""

RTOS_FAKE_TASK = """\
#pragma once
void vTaskDelay(TickType_t ticks);
"""

RTOS_FAKE_WIFI_CONF = """\
#pragma once
typedef int rtw_result_t;
#define RTW_SUCCESS 0
"""

# Mirrors the SDK names used by the real audit scan callback signature.
RTOS_FAKE_WIFI_UTIL = """\
#pragma once
#include <cstdint>
typedef struct { uint8_t val[33]; uint32_t len; } rtw_ssid_t;
typedef struct { uint8_t octet[6]; } rtw_mac_t;
typedef struct { rtw_ssid_t SSID; rtw_mac_t BSSID; int channel; } rtw_scan_result_t;
typedef struct { int scan_complete; rtw_scan_result_t ap_details; } rtw_scan_handler_result_t;
"""

PROJECT_INCLUDE_DIRS = (
    "libraries/NanoUI/src",
    "firmware/stm32/Core/NanoUI/include",
    "firmware/stm32/Core/NanoUI/assets/generated/desktop",
)

# Symbols defined by wifi_hack_core.h beyond the two moved helpers. Game code
# must not silently depend on them through the old include.
WIRELESS_CORE_SYMBOLS = (
    "WifiHackContext", "g_hackCtx", "WiFiResult", "EapolLoot", "SniffMode",
    "SNIFF_", "DEEP_SCAN_CHANNELS", "NUM_DEEP_CHANNELS", "CHANNELS_2GHZ",
    "CHANNELS_5GHZ", "appScanHandler", "isPwnagotchiMac", "HOP_INTERVAL",
    "wifi_hack_core.h", "AuditRelay", "packet-injection.h",
)


def trace_files(stderr):
    """Normalized include paths from a ``g++ -H`` trace."""
    files = set()
    for line in stderr.splitlines():
        if not line.startswith("."):
            continue
        path = line.lstrip(". ").strip().replace("\\", "/")
        if path:
            files.add(path)
    return files


def is_file(paths, suffix):
    suffix = suffix.replace("\\", "/")
    return any(path.endswith(suffix) for path in paths)


class GameRuntimeBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("g++")
        if not cls.compiler:
            raise unittest.SkipTest("host g++ is required")

        cls.folder = Path(tempfile.mkdtemp(prefix="crabit-bw16-runtime-"))
        cls.addClassCleanup(shutil.rmtree, cls.folder, ignore_errors=True)

        cls.pre_stubs = cls.folder / "sdk-stubs"
        cls.pre_stubs.mkdir()
        # Non-empty on purpose: GCC 14 omits headers whose entire body is a bare
        # "#pragma once" from the -H trace, which would weaken absence checks.
        for name in SDK_STUBS:
            (cls.pre_stubs / name).write_text(
                f"#pragma once\n// host-only stub for {name}\n", encoding="utf-8")

        cls.rtos_fakes = cls.folder / "rtos-fake"
        cls.rtos_fakes.mkdir()
        (cls.rtos_fakes / "Arduino.h").write_text(RTOS_FAKE_ARDUINO, encoding="utf-8")
        (cls.rtos_fakes / "FreeRTOS.h").write_text(RTOS_FAKE_FREERTOS, encoding="utf-8")
        (cls.rtos_fakes / "task.h").write_text(RTOS_FAKE_TASK, encoding="utf-8")
        (cls.rtos_fakes / "WiFi.h").write_text("#pragma once\n", encoding="utf-8")
        (cls.rtos_fakes / "WiFiUdp.h").write_text("#pragma once\n", encoding="utf-8")
        (cls.rtos_fakes / "wifi_conf.h").write_text(RTOS_FAKE_WIFI_CONF, encoding="utf-8")
        (cls.rtos_fakes / "wifi_util.h").write_text(RTOS_FAKE_WIFI_UTIL, encoding="utf-8")

        cls.launcher_tu = cls.folder / "launcher_tu.cpp"
        cls.launcher_tu.write_text(
            '#include "app/core/launcher.h"\n', encoding="utf-8")

    def preprocess(self, *defines):
        """Preprocess the real launcher and return (include paths, stdout)."""
        command = [self.compiler, "-E", "-H", "-x", "c++"]
        command += [f"-D{define}" for define in defines]
        command += ["-I", str(self.pre_stubs)]
        command += ["-I", str(SKETCH)]
        for directory in PROJECT_INCLUDE_DIRS:
            command += ["-I", str(ROOT / directory)]
        command += ["-o", os.devnull, str(self.launcher_tu)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr[-4000:])
        return trace_files(result.stderr), result.stdout

    def compile_probe(self, name, source):
        """Compile and run a real header probe against the FreeRTOS fakes."""
        path = self.folder / f"{name}.cpp"
        exe = self.folder / f"{name}.exe"
        path.write_text(source, encoding="utf-8")
        built = subprocess.run(
            [self.compiler, "-std=c++17", "-Wall", "-Wextra",
             "-I", str(self.rtos_fakes), "-I", str(SKETCH), str(path), "-o", str(exe)],
            cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_games_only_profile_reaches_games_without_wireless_core(self):
        files, _ = self.preprocess("CRABIT_BW16_PROFILE=4")  # games-lab
        # Positive control: the trace really renders SDK stub headers.
        self.assertTrue(is_file(files, "Arduino.h"))
        self.assertTrue(is_file(files, "app/core/app_runtime.h"))
        for game in ("castle_boy/app.h", "arduboy3d/app.h", "hopper/app.h"):
            self.assertTrue(is_file(files, f"app/apps/games/{game}"), game)
        for absent in ("app/core/wifi_hack_core.h", "packet-injection.h",
                       "AuditRelay.h", "wifi_conf.h", "wifi_util.h"):
            self.assertFalse(is_file(files, absent), f"game build pulled {absent}")

    def test_wireless_profiles_keep_the_core_and_its_runtime_helpers(self):
        for profile, name in ((5, "wifi-tools-lab"), (7, "all-lab"), (8, "network-ota-lab")):
            with self.subTest(profile=name):
                files, _ = self.preprocess(f"CRABIT_BW16_PROFILE={profile}")
                for present in ("app/core/wifi_hack_core.h", "packet-injection.h",
                                "AuditRelay.h", "app/core/app_runtime.h"):
                    self.assertTrue(is_file(files, present), f"{name} lost {present}")

    def test_opt_in_arduboy_ports_keeps_six_games_and_wireless_core(self):
        # The opt-in profile also enables Wi-Fi tools, so the wireless core is
        # expected here; the game branch itself still only adds app_runtime.h.
        files, _ = self.preprocess("CRABIT_BW16_PROFILE=5", "CRABIT_BW16_ARDUBOY_PORTS=1")
        for game in ("hollow/app.h", "samegame/app.h", "lasers/app.h",
                     "castle_boy/app.h", "arduboy3d/app.h", "hopper/app.h"):
            self.assertTrue(is_file(files, f"app/apps/games/{game}"), game)
        self.assertTrue(is_file(files, "app/core/wifi_hack_core.h"))
        self.assertTrue(is_file(files, "app/core/app_runtime.h"))

    def test_runtime_header_is_minimal_and_compiles_with_fake_rtos(self):
        self.compile_probe("runtime", r"""
#include "app/core/app_runtime.h"
#ifdef WIFI_HACK_CORE_H
#error "app_runtime.h must not pull the wireless core"
#endif
#include <cstdio>

volatile bool is_display_active = true;
unsigned long g_ticks = 0;

void vTaskDelay(TickType_t ticks) { g_ticks += ticks; }
void delayMicroseconds(unsigned int) {}

int main() {
    volatile bool* flag = &is_display_active;
    safe_delay(1);
    safe_delay(4);
    if (!*flag || g_ticks != 5) {
        std::printf("ticks=%lu flag=%d\n", g_ticks, int(*flag));
        return 1;
    }
    return 0;
}
""")

    def test_wireless_consumers_still_get_the_same_helpers(self):
        # Old consumers include only wifi_hack_core.h; it must keep exposing
        # safe_delay/is_display_active plus its own Wi-Fi context unchanged.
        self.compile_probe("wireless", r"""
#include "app/core/wifi_hack_core.h"
#ifndef APP_RUNTIME_H
#error "wifi_hack_core.h must keep providing the shared runtime helpers"
#endif
#include <cstdio>

volatile bool is_display_active = true;
unsigned long g_ticks = 0;

void vTaskDelay(TickType_t ticks) { g_ticks += ticks; }
void delayMicroseconds(unsigned int) {}

int main() {
    volatile bool* flag = &is_display_active;
    WifiHackContext* ctx = new WifiHackContext();
    ctx->reset();
    ctx->addLog(String("probe"));
    safe_delay(7);
    const bool kept = ctx->currentMode == SNIFF_STOP && ctx->intelLogs.size() == 1;
    delete ctx;
    if (!*flag || g_ticks != 7 || !kept) {
        std::printf("ticks=%lu kept=%d\n", g_ticks, int(kept));
        return 1;
    }
    return 0;
}
""")

    def test_game_sources_borrow_only_the_two_runtime_helpers(self):
        # Hopper moved from an exclusive safe_delay/is_display_active loop to
        # the managed ArduboyCompatApp FRAME lifecycle. The equivalent boundary
        # is now admission through the shared adapter, with no direct private
        # runtime-helper use in the game package.
        hopper = (GAMES / "hopper/app.h").read_text(encoding="utf-8")
        self.assertIn("class AppHopper : public ArduboyCompatApp", hopper)
        self.assertIn("prepareAdmission", hopper)
        self.assertIn("createPortState", hopper)
        self.assertNotIn("safe_delay", hopper)
        self.assertNotIn("is_display_active", hopper)
        adapter = (SKETCH / "app/compat/arduboy/ArduboyCompatApp.h").read_text(
            encoding="utf-8")
        self.assertIn("return APP_MODE_FRAME;", adapter)
        self.assertIn("void onUpdate(uint32_t now_ms) override", adapter)
        offenders = []
        for path in sorted(GAMES.rglob("*")):
            if not path.is_file() or path.suffix not in {".h", ".cpp"}:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for symbol in WIRELESS_CORE_SYMBOLS:
                if symbol in text:
                    offenders.append(f"{path.relative_to(ROOT)}: {symbol}")
        self.assertEqual(offenders, [])

    def test_launcher_no_longer_borrows_the_core_for_games(self):
        launcher = LAUNCHER.read_text(encoding="utf-8")
        games_branch = launcher.split(
            "#if CRABIT_BW16_ENABLE_GAMES || CRABIT_BW16_NETWORK_GAME", 1)[1]
        games_branch = games_branch.split("#if CRABIT_BW16_ARDUBOY_PORTS", 1)[0]
        self.assertIn('#include "app_runtime.h"', games_branch)
        self.assertNotIn('#include "wifi_hack_core.h"', games_branch)
        # The helpers themselves stay defined in exactly one minimal header.
        self.assertIn("extern volatile bool is_display_active;",
                      RUNTIME.read_text(encoding="utf-8"))
        self.assertNotIn("is_display_active", WIFI_CORE.read_text(encoding="utf-8"))
        self.assertNotIn("inline void safe_delay", WIFI_CORE.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
