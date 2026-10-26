"""Host contracts for BW16 compile/size profiles; no SDK or image build."""

import re
import shutil
import subprocess
import os
import tempfile
import unittest
import uuid
from pathlib import Path

from tools.tests import test_nanoui_original_extraction as extraction


ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = "firmware/rtl8720dn/NanoUI/app/launcher.h"
WRAPPER = ROOT / "tools/build-rtl8720dn.ps1"
PROFILES = {
    0: ("product", 0, 0, 0),
    1: ("ble-mouse-lab", 0, 0, 0),
    2: ("ble-ppt-lab", 0, 0, 0),
    3: ("ble-kbd-lab", 0, 0, 0),
    4: ("games-lab", 1, 0, 0),
    5: ("wifi-tools-lab", 0, 1, 0),
    6: ("ota-lab", 0, 0, 1),
    7: ("all-lab", 1, 1, 1),
    8: ("network-ota-lab", 0, 1, 1),
}
BLE_SELECTORS = {
    "CRABIT_BW16_IS_BLE_MOUSE_LAB": 1,
    "CRABIT_BW16_IS_BLE_PPT_LAB": 2,
    "CRABIT_BW16_IS_BLE_KBD_LAB": 3,
}
FEATURES = (
    "CRABIT_BW16_ENABLE_GAMES",
    "CRABIT_BW16_ENABLE_WIFI_LAB",
    "CRABIT_BW16_ENABLE_OTA",
)
DESKTOP_MAIN = "firmware/stm32/Core/NanoUI/platforms/desktop/main.cpp"
DESKTOP_SHUTDOWN = "    AppManager::shutdown();\n"
PROFILE_SNAPSHOT = "5e4f330"  # Completed original profile integration, not today's product.


def find_gxx():
    candidates = [r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++")]
    return next(
        (candidate for candidate in candidates if candidate and Path(candidate).is_file()), None
    )


def find_powershell():
    for name in ("powershell.exe", "pwsh"):
        candidate = shutil.which(name)
        if candidate:
            return candidate
    return None


def profile_probe(profile=None):
    checks = [
        "#include <firmware/rtl8720dn/NanoUI/src/CrabitBuildProfile.h>",
    ]
    if profile not in (None, *PROFILES):
        return "\n".join(checks) + "\n"
    checks.extend(
        [
            f'static_assert(CRABIT_BW16_PROFILE == {5 if profile is None else profile}, "profile id");',
            'static_assert(CRABIT_BW16_IS_PRODUCT == (CRABIT_BW16_PROFILE == 0), "product selector");',
        ]
    )
    if profile is None:
        checks.append('static_assert(CRABIT_BW16_NETWORK_GAME == 2, "IDE trial keeps Hopper");')
    for macro, owner in BLE_SELECTORS.items():
        checks.append(f'static_assert({macro} == (CRABIT_BW16_PROFILE == {owner}), "{macro}");')
    expected = (0, 1, 0) if profile is None else PROFILES[profile][1:]
    for macro, value in zip(FEATURES, expected):
        checks.append(f'static_assert({macro} == {value}, "{macro}");')
    checks.append('static_assert(CRABIT_BW16_ENABLE_BLE_SPAM == 0, "BLE spam stays disabled");')
    return "\n".join(checks) + "\n"


class Bw16ProfileContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = find_gxx()

    def preprocess_contract(self, profile=None):
        self.assertIsNotNone(self.compiler, "existing g++ compiler required (w64dev or PATH)")
        command = [self.compiler, "-std=c++11", "-I", str(ROOT), "-x", "c++", "-fsyntax-only", "-"]
        if profile is not None:
            command.insert(1, f"-DCRABIT_BW16_PROFILE={profile}")
        return subprocess.run(
            command, input=profile_probe(profile), text=True, capture_output=True, cwd=ROOT
        )

    def test_default_and_all_profile_contracts_with_real_gxx(self):
        default = self.preprocess_contract()
        self.assertEqual(default.returncode, 0, default.stderr)
        for profile in PROFILES:
            with self.subTest(profile=profile):
                result = self.preprocess_contract(profile)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_out_of_range_profiles_are_rejected_by_header(self):
        for profile in (-1, 9):
            with self.subTest(profile=profile):
                result = self.preprocess_contract(profile)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Unsupported CRABIT_BW16_PROFILE", result.stderr)

    def test_fixed_922_firmware_scope_is_profiles_and_desktop_shutdown_only(self):
        tracked = subprocess.check_output(
            [
                "git",
                "diff",
                "--name-only",
                "--no-renames",
                extraction.BW16_PROFILE_BASE,
                PROFILE_SNAPSHOT,
                "--",
                "firmware",
            ],
            cwd=ROOT,
            text=True,
        ).splitlines()
        changed = set(tracked)
        self.assertTrue(changed, "expected the authorized profile/launcher firmware edits")
        allowed = (
            extraction.BW16_PROFILE_PATHS
            | extraction.DISPLAY_PRODUCT_PATHS
            | extraction.SETTINGS_FLAT_PATHS
            | extraction.AMEBA_FRAME_PATHS
            | extraction.AMEBA_FRAME_DOC_PATHS
            | extraction.USB_COMPOSITE_PATHS
            | {DESKTOP_MAIN}
        )
        self.assertLessEqual(changed, allowed, sorted(changed))

    def test_fixed_922_desktop_main_has_only_the_shutdown_line(self):
        current = (ROOT / DESKTOP_MAIN).read_text(encoding="utf-8").replace("\r\n", "\n")
        baseline = extraction.original(DESKTOP_MAIN, extraction.BW16_PROFILE_BASE)
        self.assertEqual(current.count(DESKTOP_SHUTDOWN), 1)
        self.assertEqual(
            current.replace(DESKTOP_SHUTDOWN, "", 1),
            baseline,
            "desktop change must be exactly one added shutdown line",
        )
        self.assertEqual(
            current,
            baseline.replace(
                "    SDL_DestroyRenderer(renderer);",
                DESKTOP_SHUTDOWN + "    SDL_DestroyRenderer(renderer);",
                1,
            ),
            "shutdown must be immediately before renderer cleanup after the UI loop",
        )

    def test_launcher_recovers_exact_fixed_base_product_source(self):
        current = extraction.original(LAUNCHER, PROFILE_SNAPSHOT)
        baseline = extraction.original(LAUNCHER, extraction.BW16_PROFILE_BASE)
        additions = (
            "\n"
            "#if CRABIT_BW16_ENABLE_GAMES\n"
            '#include "wifi_hack_core.h"\n'
            '#include "castle_boy_app.h"\n'
            '#include "app_arduboy3d.h"\n'
            '#include "app_hopper.h"\n'
            "#endif\n"
            "#if CRABIT_BW16_ENABLE_WIFI_LAB\n"
            '#include "app_wifi_tools.h"\n'
            "#endif\n"
        )
        self.assertEqual(
            current.count(additions),
            1,
            "optional includes must be exactly those two guarded groups",
        )
        recovered = current.replace(additions, "", 1)
        wrapper = re.compile(
            r"(?m)^#if !CRABIT_BW16_ENABLE_WIFI_LAB\n"
            r"(class WifiToolsMenuApp : public Application \{.*?^\};)\n"
            r"^#endif\n",
            re.S,
        )
        recovered, count = wrapper.subn(r"\1\n", recovered, count=1)
        self.assertEqual(count, 1, "the original WiFi placeholder must be gated exactly once")
        self.assertEqual(recovered, baseline)

    def test_pair_diag_body_is_byte_identical_to_fixed_base(self):
        current = extraction.original(LAUNCHER, PROFILE_SNAPSHOT).encode("utf-8")
        baseline = extraction.original(LAUNCHER, extraction.BW16_PROFILE_BASE).encode("utf-8")
        marker = b"class PairDiagApp : public Application {"

        def body(source):
            start = source.index(marker)
            end = source.index(b"\n// ====", start)
            return source[start:end]

        self.assertEqual(body(current), body(baseline))

    def test_wrapper_profile_map_and_lab_non_export_contract(self):
        script = WRAPPER.read_text(encoding="utf-8")
        validate = re.search(r"\[ValidateSet\((.*?)\)\]", script, re.S)
        self.assertIsNotNone(validate)
        listed = re.findall(r'"([a-z0-9-]+)"', validate.group(1))
        self.assertEqual(listed, [item[0] for item in PROFILES.values()])
        mapping = re.search(r"\$profileId\s*=\s*@\{(.*?)\n\}", script, re.S)
        self.assertIsNotNone(mapping)
        pairs = re.findall(r'"([a-z0-9-]+)"\s*=\s*(\d+)', mapping.group(1))
        self.assertEqual(
            pairs, [(name, str(profile)) for profile, (name, *_flags) in PROFILES.items()]
        )
        self.assertNotIn("--export-binaries", script)
        lab_guard = re.search(
            r"if\s*\(\$Profile\s*-ne\s*\"product\"\)\s*\{(.*?)\n\s*\}", script, re.S
        )
        self.assertIsNotNone(lab_guard)
        self.assertIn("DO_NOT_FLASH.txt", lab_guard.group(1))
        self.assertIn("Not hardware accepted. Do not flash.", lab_guard.group(1))

    def make_wrapper_fixture(self, cli_exit=0, guard_exit=0):
        shell = find_powershell()
        self.assertIsNotNone(
            shell, "existing powershell.exe or pwsh is required; no installation attempted"
        )
        temporary = tempfile.TemporaryDirectory(prefix="bw16-wrapper-contract-")
        root = Path(temporary.name)
        tools = root / "tools"
        tools.mkdir()
        firmware = root / "firmware/rtl8720dn/NanoUI/src"
        firmware.mkdir(parents=True)
        (root / "firmware/shared").mkdir(parents=True)
        (root / "libraries/NanoUI").mkdir(parents=True)
        shutil.copy2(WRAPPER, tools / "build-rtl8720dn.ps1")
        (firmware / "NanoUI_Preflight.h").write_text("// test fixture\n", encoding="ascii")
        (tools / "check-bw16-image.ps1").write_text(
            "[IO.File]::WriteAllText($env:BW16_GUARD_LOG, ($args -join '|'))\n"
            "exit [int]$env:BW16_GUARD_EXIT\n",
            encoding="ascii",
        )
        bin_dir = root / "bin"
        bin_dir.mkdir()
        cli = bin_dir / "arduino-cli.cmd"
        cli.write_text(
            "@echo off\r\n"
            "setlocal EnableDelayedExpansion\r\n"
            '> "%BW16_CLI_LOG%" echo(\r\n'
            'set "NEXT="\r\nset "BUILD="\r\n'
            "for %%A in (%*) do (\r\n"
            '  >>"%BW16_CLI_LOG%" echo(%%~A\r\n'
            '  if "!NEXT!"=="1" (set "BUILD=%%~A"& set "NEXT=") else if "%%~A"=="--build-path" set "NEXT=1"\r\n'
            ")\r\n"
            'if not "%BW16_CLI_EXIT%"=="0" exit /b %BW16_CLI_EXIT%\r\n'
            'if not exist "!BUILD!" mkdir "!BUILD!"\r\n'
            '> "!BUILD!\\km0_km4_image2.bin" echo(stub\r\n'
            "exit /b 0\r\n",
            encoding="ascii",
        )
        wrapper = tools / "build-rtl8720dn.ps1"
        wrapper_text = wrapper.read_text(encoding="utf-8").replace(
            '"Local\\Crabit-BW16-Arduino-Compile"', f'"Local\\Crabit-BW16-Test-{uuid.uuid4().hex}"'
        )
        if Path(shell).name.lower().startswith("pwsh"):
            # The production wrapper targets Windows PowerShell; route only its nested
            # guard invocation to the already-installed PowerShell 7 executable.
            wrapper_text = wrapper_text.replace("powershell.exe", "pwsh")
        wrapper.write_text(wrapper_text, encoding="utf-8")
        cli_log = root / "cli-args.txt"
        guard_log = root / "guard-args.txt"
        environment = os.environ.copy()
        environment.update(
            {
                "PATH": str(bin_dir) + os.pathsep + environment.get("PATH", ""),
                "BW16_CLI_LOG": str(cli_log),
                "BW16_CLI_EXIT": str(cli_exit),
                "BW16_GUARD_LOG": str(guard_log),
                "BW16_GUARD_EXIT": str(guard_exit),
            }
        )
        return temporary, shell, wrapper, environment, cli_log, guard_log

    def invoke_wrapper(self, profile=None, ide_defaults=False, cli_exit=0, guard_exit=0):
        fixture = self.make_wrapper_fixture(cli_exit, guard_exit)
        temporary, shell, wrapper, environment, cli_log, guard_log = fixture
        arguments = [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)]
        if profile is not None:
            arguments.extend(("-Profile", profile))
        if ide_defaults:
            arguments.append("-ArduinoIdeDefaults")
        result = subprocess.run(
            arguments, cwd=wrapper.parents[1], env=environment, text=True, capture_output=True
        )
        cli_args = (
            [arg for arg in cli_log.read_text(encoding="utf-8").splitlines() if arg]
            if cli_log.exists()
            else []
        )
        guard_args = guard_log.read_text(encoding="utf-8").split("|") if guard_log.exists() else []
        marker = (
            wrapper.parents[1]
            / "build"
            / f"arduino-bw16-{profile or 'product'}"
            / "DO_NOT_FLASH.txt"
        )
        if profile is None or profile == "product":
            marker = None
        elif not ide_defaults:
            marker = wrapper.parents[1] / "build" / f"arduino-bw16-{profile}" / "DO_NOT_FLASH.txt"
        return temporary, result, cli_args, guard_args, marker

    def test_real_wrapper_invocation_profiles_defaults_and_strict_guard(self):
        for profile, (name, *_flags) in PROFILES.items():
            with self.subTest(profile=name):
                temporary, result, cli_args, guard_args, marker = self.invoke_wrapper(name)
                try:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(cli_args.count("--export-binaries"), 0)
                    self.assertTrue(
                        any(f"-DCRABIT_BW16_PROFILE={profile}" in arg for arg in cli_args)
                    )
                    self.assertIn("--build-path", cli_args)
                    output = cli_args[cli_args.index("--build-path") + 1].replace("\\", "/")
                    output_leaf = "arduino-bw16" if profile == 0 else f"arduino-bw16-{name}"
                    self.assertTrue(output.endswith(f"/build/{output_leaf}"))
                    self.assertIn("-StrictTarget", guard_args)
                    self.assertIn("-Profile", guard_args)
                    self.assertIn(name, guard_args)
                    image = guard_args[guard_args.index("-ImagePath") + 1]
                    self.assertTrue(Path(image).is_file())
                    if profile == 0:
                        self.assertFalse(any(Path(temporary.name).rglob("DO_NOT_FLASH.txt")))
                    else:
                        self.assertTrue(marker and marker.is_file())
                        self.assertIn("Do not flash", marker.read_text(encoding="ascii"))
                finally:
                    temporary.cleanup()

        temporary, result, cli_args, guard_args, _ = self.invoke_wrapper()
        try:
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(any("-DCRABIT_BW16_PROFILE=0" in arg for arg in cli_args))
            self.assertIn("--build-path", cli_args)
        finally:
            temporary.cleanup()

        temporary, result, cli_args, guard_args, _ = self.invoke_wrapper(ide_defaults=True)
        try:
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("--build-property", cli_args)
            self.assertFalse(any("-DCRABIT_BW16_PROFILE=" in arg for arg in cli_args))
            self.assertEqual(cli_args.count("--export-binaries"), 0)
            output = cli_args[cli_args.index("--build-path") + 1].replace("\\", "/")
            self.assertTrue(output.endswith("/build/arduino-bw16-ide"))
            self.assertIn("-StrictTarget", guard_args)
        finally:
            temporary.cleanup()

    def test_wrapper_rejects_invalid_profile_and_lab_ide_defaults_before_compile(self):
        for profile, ide in (("unknown-lab", False), ("games-lab", True)):
            with self.subTest(profile=profile, ide_defaults=ide):
                temporary, result, cli_args, guard_args, _ = self.invoke_wrapper(
                    profile, ide_defaults=ide
                )
                try:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(cli_args, [])
                    self.assertEqual(guard_args, [])
                finally:
                    temporary.cleanup()

    def test_wrapper_guard_failure_propagates_and_compile_failure_never_calls_guard(self):
        temporary, result, cli_args, guard_args, _ = self.invoke_wrapper("games-lab", guard_exit=23)
        try:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("range guard failed with exit code 23", result.stderr)
            self.assertTrue(guard_args)
            self.assertIn("-StrictTarget", guard_args)
        finally:
            temporary.cleanup()

        temporary, result, cli_args, guard_args, _ = self.invoke_wrapper("ota-lab", cli_exit=17)
        try:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Arduino CLI compile failed with exit code 17", result.stderr)
            self.assertEqual(guard_args, [])
            self.assertEqual(cli_args.count("--export-binaries"), 0)
        finally:
            temporary.cleanup()


if __name__ == "__main__":
    unittest.main()
