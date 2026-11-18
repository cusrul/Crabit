"""D0/D1/D2 module layout contract: no flat Core/Inc or Core/Src copies remain.

The migration was verified once against the pre-move commit (normalized text:
board.cpp/companion_ble.h identical, companion_ble.cpp identical apart from the
two canonical shared includes; D2 files identical apart from the two canonical
shared includes in network_weather.h). This permanent test locks the resulting
module directories, the single CMake compile location, the include roots that
make the existing bare includes resolve, and the production function surface. It
does not pin a repository commit or a full-file hash, so ordinary logic edits
stay legal.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STM32 = ROOT / "firmware/stm32"
PRODUCT = STM32 / "product"
BOARD_DIR = PRODUCT / "board"
COMPANION_DIR = PRODUCT / "features/companion"
CMAKE = STM32 / "CMakeLists.txt"
D1_MODULES = (
    ("link_queue", PRODUCT / "services/link"),
    ("settings_service", PRODUCT / "services/settings"),
    ("diagnostic_service", PRODUCT / "services/diagnostics"),
    ("audit_service", PRODUCT / "features/audit"),
)
D2_MODULES = (
    ("network_weather", PRODUCT / "features/network"),
    ("companion_trace", PRODUCT / "services/diagnostics"),
)

OLD_FLAT = (
    STM32 / "Core/Inc/board.h",
    STM32 / "Core/Src/board.cpp",
    STM32 / "Core/Inc/companion_ble.h",
    STM32 / "Core/Src/companion_ble.cpp",
) + tuple(
    STM32 / f"Core/{directory}/{name}.{suffix}"
    for name, _ in D1_MODULES + D2_MODULES
    for directory, suffix in (("Inc", "h"), ("Src", "cpp"))
)
MODULES = (
    (BOARD_DIR / "board.h", BOARD_DIR / "board.cpp"),
    (COMPANION_DIR / "companion_ble.h", COMPANION_DIR / "companion_ble.cpp"),
) + tuple((directory / f"{name}.h", directory / f"{name}.cpp")
          for name, directory in D1_MODULES + D2_MODULES)
OLD_CMAKE_PATHS = (
    "Core/Src/board.cpp",
    "Core/Src/companion_ble.cpp",
) + tuple(f"Core/Src/{name}.cpp" for name, _ in D1_MODULES + D2_MODULES)
NEW_CMAKE_PATHS = (
    "product/board/board.cpp",
    "product/features/companion/companion_ble.cpp",
) + tuple((directory / f"{name}.cpp").relative_to(STM32).as_posix()
          for name, directory in D1_MODULES + D2_MODULES)


class ProductModuleLayoutTests(unittest.TestCase):
    def cmake_text(self):
        return CMAKE.read_text(encoding="utf-8")

    def test_flat_core_paths_are_gone_and_modules_are_paired(self):
        for path in OLD_FLAT:
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertFalse(path.exists(), "old flat path must not remain")
        for header, source in MODULES:
            with self.subTest(module=header.parent.relative_to(ROOT)):
                self.assertTrue(header.is_file())
                self.assertTrue(source.is_file())
                self.assertGreater(len(source.read_text(encoding="utf-8").splitlines()), 20)
        # No forwarding shim sneaked into Core/Inc or Core/Src under another name.
        names = {path.name for path in (STM32 / "Core/Inc").iterdir()}
        names.update(path.name for path in (STM32 / "Core/Src").iterdir())
        self.assertNotIn("board.h", names)
        self.assertNotIn("companion_ble.h", names)
        for name, _ in D1_MODULES + D2_MODULES:
            self.assertNotIn(f"{name}.h", names)
            self.assertNotIn(f"{name}.cpp", names)

    def test_cmake_has_one_compile_location_per_module(self):
        cmake = self.cmake_text()
        for old in OLD_CMAKE_PATHS:
            self.assertNotIn(old, cmake, f"stale flat source in CMake: {old}")
        for new in NEW_CMAKE_PATHS:
            self.assertEqual(cmake.count(new), 1, f"{new} must appear exactly once")
        # Scan every live STM32 build file, not just the top-level list.
        build_files = list(STM32.rglob("CMakeLists.txt")) + list(STM32.rglob("*.cmake"))
        for name in (source.name for _, source in MODULES):
            hits = [
                path.relative_to(ROOT)
                for path in build_files
                for line in path.read_text(encoding="utf-8", errors="ignore").splitlines()
                if name in line and not line.lstrip().startswith("#")
            ]
            self.assertEqual(len(hits), 1, f"{name} compiled from exactly one place: {hits}")

    def test_include_roots_keep_existing_bare_includes_resolving(self):
        cmake = self.cmake_text()
        for root in (
            "../shared",
            "product/board",
            "product/features/companion",
            "product/services/link",
            "product/services/settings",
            "product/services/diagnostics",
            "product/features/audit",
            "product/features/network",
        ):
            with self.subTest(root=root):
                self.assertIn(f"${{CMAKE_CURRENT_SOURCE_DIR}}/{root}", cmake)
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "existing host g++ is required")
        with tempfile.TemporaryDirectory(prefix="crabit-module-includes-") as tmp:
            source = Path(tmp) / "includes.cpp"
            source.write_text(
                '#include "board.h"\n#include "companion_ble.h"\n'
                "int main() { return 0; }\n",
                encoding="utf-8",
            )
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsyntax-only",
                    f"-I{BOARD_DIR}",
                    f"-I{COMPANION_DIR}",
                    str(source),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

    def test_d1_d2_bare_headers_resolve_using_cmake_include_roots(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "existing host g++ is required")
        roots = re.findall(r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^\s)]+)", self.cmake_text())
        with tempfile.TemporaryDirectory(prefix="crabit-d1-includes-") as tmp:
            temporary = Path(tmp)
            # Only FreeRTOS types are faked; product and shared headers are real.
            (temporary / "FreeRTOS.h").write_text(
                "#pragma once\n#include <stdint.h>\n"
                "typedef uint32_t TickType_t; typedef unsigned UBaseType_t;\n",
                encoding="utf-8",
            )
            (temporary / "queue.h").write_text(
                "#pragma once\ntypedef void* QueueHandle_t;\n", encoding="utf-8"
            )
            source = temporary / "includes.cpp"
            source.write_text(
                "".join(f'#include "{name}.h"\n' for name, _ in D1_MODULES + D2_MODULES)
                + "int main() { return 0; }\n", encoding="utf-8"
            )
            built = subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-fsyntax-only", f"-I{temporary}",
                 *(f"-I{STM32 / root}" for root in roots), str(source)],
                capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

    def test_d1_sources_retain_explicit_dispatch_module_order(self):
        sources = re.search(
            r"target_sources\(\$\{CMAKE_PROJECT_NAME\} PRIVATE(.*?)\)",
            self.cmake_text(), re.DOTALL,
        )
        self.assertIsNotNone(sources)
        lines = [line.split("#", 1)[0].strip()
                 for line in sources.group(1).splitlines() if line.strip()]
        start = lines.index("product/board/board.cpp")
        self.assertEqual(lines[start:start + 6], [
            "product/board/board.cpp",
            "product/services/settings/settings_service.cpp",
            "product/services/link/link_queue.cpp",
            "product/features/audit/audit_service.cpp",
            "product/services/diagnostics/diagnostic_service.cpp",
            "Core/Src/i2c_bus.cpp",
        ])
        # D2 kept its original list slots: same neighbours, no reshuffle.
        d2 = lines.index("product/features/network/network_weather.cpp")
        self.assertEqual(lines[d2 - 1:d2 + 3], [
            "product/features/companion/companion_ble.cpp",
            "product/features/network/network_weather.cpp",
            "product/services/diagnostics/companion_trace.cpp",
            "Core/Src/app_bw16_flash.cpp",
        ])

    def test_d1_d2_modules_have_no_relative_shared_headers_or_duplicate_copies(self):
        for name, directory in D1_MODULES + D2_MODULES:
            for suffix in ("h", "cpp"):
                expected = directory / f"{name}.{suffix}"
                copies = [path for root in (STM32 / "Core", PRODUCT)
                          for path in root.rglob(expected.name)]
                self.assertEqual(copies, [expected])
                self.assertNotRegex(expected.read_text(encoding="utf-8"),
                                    r'#include\s+"[^"\n]*\.\./[^"\n]*shared/')

    def test_moved_companion_source_uses_canonical_shared_headers(self):
        source = (COMPANION_DIR / "companion_ble.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "companion_session.h"', source)
        self.assertIn('#include "companion_probe.h"', source)
        self.assertNotIn("../../../shared/", source)

    def test_moved_sources_keep_the_production_function_surface(self):
        board = (BOARD_DIR / "board.cpp").read_text(encoding="utf-8")
        for marker in (
            "class Board",  # via board.h
            "void Board::init()",
            "void Board::startTasks()",
            "Crabit::CompanionBle::UiHooks companionUiHooks()",
            "Crabit::CompanionBle::HardwareHooks companionHardwareHooks()",
            "bool bindCompanionPorts()",
        ):
            if marker == "class Board":
                self.assertIn(marker, (BOARD_DIR / "board.h").read_text(encoding="utf-8"))
                continue
            self.assertIn(marker, board)
        header = (COMPANION_DIR / "companion_ble.h").read_text(encoding="utf-8")
        self.assertIn("namespace Crabit { namespace CompanionBle {", header)
        self.assertIn("bool bind(const UiHooks& ui, const HardwareHooks& hardware)", header)
        source = (COMPANION_DIR / "companion_ble.cpp").read_text(encoding="utf-8")
        for marker in (
            "namespace Crabit { namespace CompanionBle {",
            "bool active()",
            "bool hardwareActive()",
            "bool legacyAutostartAllowed()",
            "void observeProbePong(const char* value)",
            "const char* status()",
            "uint16_t sessionId()",
            "bool start()",
            "bool stop()",
            "void observeModeStatus(const char* value)",
            "void receive(uint16_t id, const char* value)",
            "void tick()",
        ):
            self.assertIn(marker, source)
        # Behavior-critical status text stayed in the service implementation.
        for status in ("LEGACY BLE ON", "PONG SEEN", "PING TIMEOUT", "BLE FAILED"):
            self.assertIn(status, source)

    def test_d2_modules_keep_the_production_function_surface(self):
        header = (PRODUCT / "features/network/network_weather.h").read_text(encoding="utf-8")
        self.assertIn("namespace Crabit { namespace NetworkWeather {", header)
        for marker in (
            "bool receive(const CrabitWire::Message& message)",
            "void init()",
            "bool begin()",
            "void tick()",
            "void cancel()",
            "bool busy()",
            "const char* status()",
        ):
            self.assertIn(marker, header)
        trace_header = (PRODUCT / "services/diagnostics/companion_trace.h").read_text(encoding="utf-8")
        self.assertIn("namespace Crabit { namespace CompanionTrace {", trace_header)
        for marker in (
            "void record(Event event, uint16_t session = 0, uint32_t a = 0, uint32_t b = 0)",
            "void message(const char* topic, const char* value, uint16_t session)",
            "void poll()",
        ):
            self.assertIn(marker, trace_header)
        trace_source = (PRODUCT / "services/diagnostics/companion_trace.cpp").read_text(encoding="utf-8")
        # Raw USB trace framing and the flash-exclusion guard stayed in place.
        self.assertIn("CRABIT:RADIO", trace_source)
        self.assertIn("bw16_flash_mode", trace_source)

    def test_live_tools_and_tests_do_not_reference_flat_paths(self):
        stale = [path.relative_to(ROOT).as_posix() for path in OLD_FLAT]
        stale += ["Core/Inc/companion_ble.h", "Core/Src/companion_ble.cpp"]
        stale += ["Core/Inc/board.h", "Core/Src/board.cpp"]
        stale = sorted(set(stale))
        scan = [CMAKE]
        # Include nested host fixtures and non-Python build/tool entry points too.
        active_suffixes = {".py", ".ps1", ".sh", ".bat", ".cmd", ".cmake",
                           ".txt", ".h", ".hpp", ".c", ".cpp"}
        for root in (ROOT / "tools", ROOT / "tests", ROOT / "apps/mobile/native"):
            scan += sorted(path for path in root.rglob("*")
                           if path.is_file() and path.suffix in active_suffixes
                           and "__pycache__" not in path.parts)
        offenders = []
        for path in scan:
            if path.name == Path(__file__).name:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for needle in stale:
                if needle in text or needle.replace("firmware/stm32/", "") in text:
                    offenders.append(f"{path.relative_to(ROOT).as_posix()}: {needle}")
        self.assertEqual(offenders, [], "\n".join(offenders))


if __name__ == "__main__":
    unittest.main()
