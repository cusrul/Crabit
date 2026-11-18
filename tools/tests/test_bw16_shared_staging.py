"""Offline stage-list/parity checks for the single BW16 shared-header manifest.

Both BW16 entry points must read tools/bw16_shared_headers.json; the header list
lives nowhere else. No SDK build, compiler run, serial port or device is touched.
"""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tools/bw16_shared_headers.json"
WRAPPER = ROOT / "tools/build-rtl8720dn.ps1"
SPLIT = ROOT / "tools/build_bw16_split_games.py"
SKETCH = ROOT / "firmware/rtl8720dn/NanoUI"
STAGED = SKETCH / "src/crabit_shared"
SOURCE = ROOT / "firmware/shared"
HEADERS = ("command_router.h", "comms_link.h", "topics.h", "weather_contract.h")
SHIM = '#include "../../../../shared/{}"'

SPEC = importlib.util.spec_from_file_location("split_games_staging", SPLIT)
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


def find_powershell():
    for name in ("powershell.exe", "pwsh"):
        candidate = shutil.which(name)
        if candidate:
            return candidate
    return None


def base_manifest():
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def with_headers(*headers):
    return lambda data: {**data, "headers": list(headers)}


def without_comment(data):
    return {key: value for key, value in data.items() if key != "$comment"}


# One matrix, run verbatim against the Python loader and the PowerShell wrapper.
# Each entry is a manifest mutation that both entry points must reject, with the
# message fragment both sides must report for that specific rule.
ROOT_RULE = "must stage firmware/shared into src/crabit_shared"
NAME_RULE = "header must be a plain file name"
ARRAY_RULE = "must list headers as a non-empty array"
DUPLICATE_RULE = "lists a header more than once"
UNKNOWN_RULE = "unexpected keys"
COMMENT_RULE = "$comment must document the source of truth"
UNSAFE_MANIFESTS = (
    ("parent traversal header", with_headers("../command_router.h"), NAME_RULE),
    ("rooted header", with_headers("/command_router.h"), NAME_RULE),
    ("drive header", with_headers("C:/command_router.h"), NAME_RULE),
    ("nested header", with_headers("nested/topics.h"), NAME_RULE),
    ("header without suffix", with_headers("topics"), NAME_RULE),
    ("double-dot header", with_headers("foo..bar.h"), NAME_RULE),
    ("trailing newline header", with_headers("topics.h\n"), NAME_RULE),
    ("leading-dot header", with_headers(".topics.h"), NAME_RULE),
    ("empty header list", with_headers(), ARRAY_RULE),
    ("header list as string", lambda data: {**data, "headers": "topics.h"}, ARRAY_RULE),
    ("header list as number", lambda data: {**data, "headers": 5}, ARRAY_RULE),
    ("case-insensitive duplicate", with_headers(*HEADERS, "Topics.h"), DUPLICATE_RULE),
    (
        "header without sketch target",
        with_headers(*HEADERS, "audit_contract.h"),
        "has no sketch target",
    ),
    (
        "trailing slash source root",
        lambda data: {**data, "source_root": "firmware/shared/"},
        ROOT_RULE,
    ),
    (
        "leading slash source root",
        lambda data: {**data, "source_root": "/firmware/shared/"},
        ROOT_RULE,
    ),
    ("backslash source root", lambda data: {**data, "source_root": "firmware\\shared"}, ROOT_RULE),
    ("parent source root", lambda data: {**data, "source_root": "../firmware/shared"}, ROOT_RULE),
    ("lower-case source root", lambda data: {**data, "source_root": "Firmware/Shared"}, ROOT_RULE),
    (
        "trailing slash staged root",
        lambda data: {**data, "staged_root": "/src/crabit_shared/"},
        ROOT_RULE,
    ),
    ("other staged root", lambda data: {**data, "staged_root": "src/other"}, ROOT_RULE),
    ("unknown key", lambda data: {**data, "unexpected": True}, UNKNOWN_RULE),
    ("missing comment", without_comment, COMMENT_RULE),
    ("non-string comment", lambda data: {**data, "$comment": 5}, COMMENT_RULE),
)
RAW_INVALID_MANIFESTS = (
    ("{ not json", "unreadable"),
    ("[1]", "must be a JSON object"),
)


class ManifestContractTests(unittest.TestCase):
    def test_manifest_is_the_only_header_list_and_names_the_source_of_truth(self):
        data = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.assertEqual(set(data), {"$comment", "source_root", "staged_root", "headers"})
        self.assertIn("firmware/shared", data["$comment"])
        self.assertIn("source of truth", data["$comment"])
        self.assertEqual(data["source_root"], "firmware/shared")
        self.assertEqual(data["staged_root"], "src/crabit_shared")
        self.assertEqual(sorted(data["headers"]), sorted(HEADERS))

    def test_both_entry_points_read_the_same_manifest_not_a_second_list(self):
        wrapper = WRAPPER.read_text(encoding="utf-8")
        split = SPLIT.read_text(encoding="utf-8")
        for name, text in (("wrapper", wrapper), ("split builder", split)):
            self.assertIn("bw16_shared_headers.json", text, name)
            for number, line in enumerate(text.splitlines(), 1):
                listed = [header for header in HEADERS if header in line]
                self.assertLessEqual(
                    len(listed),
                    1,
                    f"{name}:{number} hard-codes a shared-header list: {line.strip()}",
                )
        self.assertIn("ConvertFrom-Json", wrapper)
        self.assertRegex(wrapper, r"foreach \(\$shared in \$sharedHeaders\)")
        self.assertEqual(build.SHARED_MANIFEST.as_posix(), "tools/bw16_shared_headers.json")

    def test_every_manifest_header_maps_to_its_sketch_target(self):
        data = json.loads(MANIFEST.read_text(encoding="utf-8"))
        for name in data["headers"]:
            origin, target = SOURCE / name, STAGED / name
            self.assertTrue(origin.is_file(), origin)
            self.assertTrue(target.is_file(), target)
            if target.read_bytes() == origin.read_bytes():
                continue
            self.assertIn(SHIM.format(name), target.read_text(encoding="utf-8"), target)
        for path in (SOURCE, STAGED):
            for name in HEADERS:
                self.assertTrue((path / name).is_file(), path / name)

    def test_wrapper_parses_as_powershell(self):
        shell = find_powershell()
        if not shell:
            self.skipTest("existing powershell.exe or pwsh is required")
        environment = os.environ.copy()
        environment["BW16_PARSE_PATH"] = str(WRAPPER)
        completed = subprocess.run(
            [
                shell,
                "-NoProfile",
                "-Command",
                "$t=$null; $e=$null; "
                "[void][System.Management.Automation.Language.Parser]::ParseFile("
                "$env:BW16_PARSE_PATH,[ref]$t,[ref]$e); "
                "if ($e) { $e | ForEach-Object { $_.Message }; exit 1 }; exit 0",
            ],
            env=environment,
            text=True,
            capture_output=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)


class ManifestLoaderTests(unittest.TestCase):
    def make_repo(self, root, data, headers=(*HEADERS, "audit_contract.h"), targets=HEADERS):
        repo = root / "repo"
        (repo / "firmware/shared").mkdir(parents=True)
        (repo / "firmware/rtl8720dn/NanoUI/src/crabit_shared").mkdir(parents=True)
        (repo / "tools").mkdir()
        for name in headers:
            (repo / "firmware/shared" / name).write_text(f"// canonical {name}\n", encoding="utf-8")
        for name in targets:
            (repo / "firmware/rtl8720dn/NanoUI/src/crabit_shared" / name).write_text(
                f"// stale {name}\n", encoding="utf-8"
            )
        manifest = repo / "tools/bw16_shared_headers.json"
        if isinstance(data, str):
            manifest.write_text(data, encoding="utf-8")
        else:
            manifest.write_text(json.dumps(data), encoding="utf-8")
        return repo, manifest

    def test_valid_manifest_resolves_manifest_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo, manifest = self.make_repo(Path(tmp), base_manifest())
            sketch = repo / "firmware/rtl8720dn/NanoUI"
            pairs = build.shared_staging(manifest, repo, sketch)
            self.assertEqual(
                [relative for _origin, relative in pairs],
                [f"src/crabit_shared/{name}" for name in HEADERS],
            )

    def test_unsafe_or_incomplete_manifests_fail_early(self):
        for label, mutate, expected in UNSAFE_MANIFESTS:
            with self.subTest(case=label):
                with tempfile.TemporaryDirectory() as tmp:
                    repo, manifest = self.make_repo(Path(tmp), mutate(base_manifest()))
                    with self.assertRaises(ValueError) as raised:
                        build.shared_staging(manifest, repo, repo / "firmware/rtl8720dn/NanoUI")
                    self.assertIn(expected, str(raised.exception))

    def test_raw_invalid_manifests_fail_early(self):
        for text, expected in RAW_INVALID_MANIFESTS:
            with self.subTest(manifest=text):
                with tempfile.TemporaryDirectory() as tmp:
                    repo, manifest = self.make_repo(Path(tmp), text)
                    with self.assertRaises(ValueError) as raised:
                        build.shared_staging(manifest, repo, repo / "firmware/rtl8720dn/NanoUI")
                    self.assertIn(expected, str(raised.exception))

    def test_header_absent_from_source_and_absent_shim_fail_early(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = base_manifest()
            data["headers"] = [*data["headers"], "not_a_real_contract.h"]
            repo, manifest = self.make_repo(Path(tmp), data)
            with self.assertRaises(ValueError) as raised:
                build.shared_staging(manifest, repo, repo / "firmware/rtl8720dn/NanoUI")
            self.assertIn("missing from firmware/shared", str(raised.exception))
        with tempfile.TemporaryDirectory() as tmp:
            repo, manifest = self.make_repo(Path(tmp), base_manifest(), targets=HEADERS[:-1])
            with self.assertRaises(ValueError) as raised:
                build.shared_staging(manifest, repo, repo / "firmware/rtl8720dn/NanoUI")
            self.assertIn("no sketch target", str(raised.exception))

    def test_absent_manifest_fails_early(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo, manifest = self.make_repo(Path(tmp), base_manifest())
            manifest.unlink()
            with self.assertRaises(ValueError) as raised:
                build.shared_staging(manifest, repo, repo / "firmware/rtl8720dn/NanoUI")
            self.assertIn("missing", str(raised.exception))

    def test_staging_copies_identical_bytes_for_every_manifest_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            sketch = Path(tmp) / "NanoUI"
            (sketch / "src/crabit_shared").mkdir(parents=True)
            for name in HEADERS:
                (sketch / "src/crabit_shared" / name).write_text(
                    SHIM.format(name) + "\n", encoding="utf-8"
                )
            pairs = build.shared_staging(MANIFEST, ROOT, sketch)
            build.stage_shared_headers(sketch, pairs)
            for name in HEADERS:
                self.assertEqual(
                    (sketch / "src/crabit_shared" / name).read_bytes(),
                    (SOURCE / name).read_bytes(),
                    name,
                )
            self.assertEqual(
                sorted(p.name for p in (sketch / "src/crabit_shared").iterdir()), sorted(HEADERS)
            )


class WrapperStagingTests(unittest.TestCase):
    """Real PowerShell invocation with a mock arduino-cli; no SDK build happens."""

    def make_fixture(self, manifest):
        shell = find_powershell()
        if not shell:
            self.skipTest("existing powershell.exe or pwsh is required")
        temporary = tempfile.TemporaryDirectory(prefix="bw16-shared-staging-")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        tools, bin_dir = root / "tools", root / "bin"
        tools.mkdir()
        bin_dir.mkdir()
        sketch = root / "firmware/rtl8720dn/NanoUI"
        (sketch / "src").mkdir(parents=True)
        (sketch / "src/NanoUI_Preflight.h").write_text("// fixture\n", encoding="ascii")
        (sketch / "ble_auth.local.h").write_text(
            "// fixture only, never a real code\n", encoding="ascii"
        )
        (root / "libraries/NanoUI").mkdir(parents=True)
        shared = root / "firmware/shared"
        shared.mkdir(parents=True, exist_ok=True)
        (sketch / "src/crabit_shared").mkdir(parents=True, exist_ok=True)
        for name in (*HEADERS, "audit_contract.h"):
            (shared / name).write_text(f"// canonical {name}\n", encoding="utf-8")
        for name in HEADERS:
            (sketch / "src/crabit_shared" / name).write_text(
                SHIM.format(name) + "\n", encoding="utf-8"
            )
        (tools / "bw16_shared_headers.json").write_text(manifest, encoding="utf-8")
        (tools / "check-bw16-image.ps1").write_text(
            "[IO.File]::WriteAllText($env:BW16_GUARD_LOG, ($args -join '|'))\nexit 0\n",
            encoding="ascii",
        )
        wrapper = tools / "build-rtl8720dn.ps1"
        text = WRAPPER.read_text(encoding="utf-8").replace(
            '"Local\\Crabit-BW16-Arduino-Compile"', f'"Local\\Crabit-BW16-Test-{uuid.uuid4().hex}"'
        )
        if Path(shell).name.lower().startswith("pwsh"):
            text = text.replace("powershell.exe", "pwsh")
        wrapper.write_text(text, encoding="utf-8")
        cli = bin_dir / "arduino-cli.cmd"
        cli.write_text(
            "@echo off\r\n"
            "setlocal EnableDelayedExpansion\r\n"
            '>>"%BW16_CLI_LOG%" echo(\r\n'
            'set "NEXT="\r\nset "BUILD="\r\nset "LAST="\r\n'
            "for %%A in (%*) do (\r\n"
            '  >>"%BW16_CLI_LOG%" echo(%%~A\r\n'
            '  if "!NEXT!"=="1" (set "BUILD=%%~A"& set "NEXT=") else if "%%~A"=="--build-path" '
            'set "NEXT=1"\r\n'
            '  set "LAST=%%~A"\r\n'
            ")\r\n"
            'if not "%BW16_CLI_EXIT%"=="0" exit /b %BW16_CLI_EXIT%\r\n'
            'if not exist "!BUILD!" mkdir "!BUILD!"\r\n'
            '> "!BUILD!\\km0_km4_image2.bin" echo(stub\r\n'
            'if defined BW16_STAGE_SNAPSHOT xcopy "!LAST!\\src\\crabit_shared" '
            '"%BW16_STAGE_SNAPSHOT%\\crabit_shared" /E /I /Q /Y >nul\r\n'
            "exit /b 0\r\n",
            encoding="ascii",
        )
        snapshot = root / "snapshot"
        environment = os.environ.copy()
        environment.update(
            {
                "PATH": str(bin_dir) + os.pathsep + environment.get("PATH", ""),
                "BW16_CLI_LOG": str(root / "cli-args.txt"),
                "BW16_CLI_EXIT": "0",
                "BW16_STAGE_SNAPSHOT": str(snapshot),
                "BW16_GUARD_LOG": str(root / "guard-args.txt"),
            }
        )
        completed = subprocess.run(
            [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)],
            cwd=root,
            env=environment,
            text=True,
            capture_output=True,
        )
        return completed, shared, snapshot, root / "cli-args.txt"

    def test_wrapper_stages_exactly_the_manifest_headers(self):
        completed, shared, snapshot, cli_log = self.make_fixture(
            MANIFEST.read_text(encoding="utf-8")
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertTrue(cli_log.is_file(), "fixture mock compile was not invoked")
        staged = snapshot / "crabit_shared"
        self.assertEqual(sorted(p.name for p in staged.iterdir()), sorted(HEADERS))
        for name in HEADERS:
            self.assertEqual((staged / name).read_bytes(), (shared / name).read_bytes(), name)

    def test_wrapper_rejects_the_same_manifest_matrix_before_any_compile(self):
        manifests = [
            (label, json.dumps(mutate(base_manifest())), expected)
            for label, mutate, expected in UNSAFE_MANIFESTS
        ]
        manifests += [("raw invalid", text, expected) for text, expected in RAW_INVALID_MANIFESTS]
        for label, text, expected in manifests:
            with self.subTest(case=label):
                completed, _shared, snapshot, cli_log = self.make_fixture(text)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn(expected, completed.stderr)
                self.assertFalse(cli_log.exists(), "manifest rejection must precede any compile")
                self.assertFalse(snapshot.exists())


if __name__ == "__main__":
    unittest.main()
