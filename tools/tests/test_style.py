"""Isolated tests for the registered-scope formatter runner."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import style


class StyleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.manifest = self.root / "scope.json"
        (self.root / "pyproject.toml").write_text("[tool.black]\n", encoding="utf-8")
        (self.root / ".clang-format").write_text("BasedOnStyle: Google\n", encoding="utf-8")
        self.cpp = self.root / "src" / "unit.cpp"
        self.py = self.root / "src" / "unit.py"
        self.dart = self.root / "app" / "unit.dart"
        for path in (self.cpp, self.py, self.dart):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("content\n", encoding="utf-8")
        self.data = {
            "schema_version": 1,
            "versions": style.VERSIONS.copy(),
            "targets": {
                "cpp": ["src/unit.cpp"],
                "python": ["src/unit.py"],
                "dart": ["app/unit.dart"],
            },
        }
        self.save()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def save(self) -> None:
        self.manifest.write_text(json.dumps(self.data), encoding="utf-8")

    def load(self) -> dict[str, list[Path]]:
        return style.load_targets(self.root, self.manifest)

    def test_manifest_selects_exact_registered_files_and_omits_unlisted_firmware(self) -> None:
        unlisted = self.root / "firmware" / "unlisted.ino"
        unlisted.parent.mkdir()
        unlisted.write_text("untouched", encoding="utf-8")
        targets = self.load()
        self.assertEqual(targets["cpp"], [self.cpp.resolve()])
        self.assertNotIn(unlisted.resolve(), targets["cpp"])

    def test_rejects_traversal_absolute_duplicate_and_empty_language(self) -> None:
        for bad in ("../escape.cpp", str(self.cpp)):
            with self.subTest(target=bad):
                self.data["targets"]["cpp"] = [bad]
                self.save()
                with self.assertRaises(style.StyleError):
                    self.load()
        self.data["targets"] = {
            "cpp": ["src/unit.cpp", "src/unit.cpp"],
            "python": ["src/unit.py"],
            "dart": ["app/unit.dart"],
        }
        self.save()
        with self.assertRaisesRegex(style.StyleError, "duplicate"):
            self.load()
        self.data["targets"]["cpp"] = ["src/unit.cpp"]
        self.data["targets"]["dart"] = []
        self.save()
        with self.assertRaisesRegex(style.StyleError, "non-empty"):
            self.load()

    def test_rejects_generated_and_resource_files_and_skips_generated_in_directories(self) -> None:
        generated = self.root / "src" / "models.g.dart"
        generated.write_text("generated", encoding="utf-8")
        resources = self.root / "resources" / "bad.py"
        resources.parent.mkdir()
        resources.write_text("resource", encoding="utf-8")
        for target in ("src/models.g.dart", "resources/bad.py"):
            self.data["targets"]["python"] = [target]
            self.save()
            with self.assertRaises(style.StyleError):
                self.load()
        self.data["targets"]["python"] = ["src"]
        self.save()
        self.assertEqual(self.load()["python"], [self.py.resolve()])

    def test_rejects_wrong_pinned_version_and_tool_failure(self) -> None:
        class Result:
            returncode = 0
            stdout = "black, 99.0.0"
            stderr = ""

        with patch.object(style, "resolve_tool", return_value="black"):
            with self.assertRaisesRegex(style.StyleError, "24.4.2"):
                style.run("python", "check", [self.py], self.root, runner=lambda *a, **k: Result())
            Result.stdout = "black, 24.4.2"
            calls = 0

            def failed_formatter(*args: object, **kwargs: object) -> Result:
                nonlocal calls
                calls += 1
                result = Result()
                result.returncode = 127 if calls == 2 else 0
                return result

            with self.assertRaisesRegex(style.StyleError, "failed"):
                style.run("python", "check", [self.py], self.root, runner=failed_formatter)

    def test_check_is_nonmutating_and_fix_uses_only_selected_files(self) -> None:
        calls: list[tuple[list[str], dict[str, object]]] = []

        class Result:
            returncode = 0
            stdout = "Black 24.4.2"
            stderr = ""

        def runner(argv: list[str], **kwargs: object) -> Result:
            calls.append((argv, kwargs))
            return Result()

        with patch.object(style, "resolve_tool", return_value="black"):
            style.run("python", "check", [self.py.resolve()], self.root, runner)
            check_args = calls[-1][0]
            self.assertIn("--check", check_args)
            self.assertIn(str(self.py.resolve()), check_args)
            self.assertEqual(self.py.read_text(encoding="utf-8"), "content\n")
            style.run("python", "fix", [self.py.resolve()], self.root, runner)
            fix_args = calls[-1][0]
            self.assertNotIn("--check", fix_args)
            self.assertEqual(fix_args[-1], str(self.py.resolve()))
            self.assertEqual(len(calls), 4)  # version probe and formatter per action

    def test_cpp_and_dart_use_required_check_and_fix_switches(self) -> None:
        calls: list[list[str]] = []

        class Result:
            returncode = 0
            stdout = "clang-format version 19.1.1"  # replaced for Dart below
            stderr = ""

        def runner(argv: list[str], **kwargs: object) -> Result:
            calls.append(argv)
            result = Result()
            if argv[0] == "dart":
                result.stdout = ""
                result.stderr = "Dart SDK version: 3.11.4"
            return result

        for language, tool, path in (
            ("cpp", "clang-format", self.cpp),
            ("dart", "dart", self.dart),
        ):
            with (
                self.subTest(language=language),
                patch.object(style, "resolve_tool", return_value=tool),
            ):
                style.run(language, "check", [path.resolve()], self.root, runner)
                check_args = calls[-1]
                if language == "cpp":
                    self.assertIn("--fallback-style=none", check_args)
                    self.assertIn("--dry-run", check_args)
                    self.assertIn("--Werror", check_args)
                else:
                    self.assertEqual(
                        check_args[1:4], ["format", "--output=none", "--set-exit-if-changed"]
                    )
                style.run(language, "fix", [path.resolve()], self.root, runner)
                fix_args = calls[-1]
                self.assertIn("-i" if language == "cpp" else "--output=write", fix_args)
                self.assertEqual(fix_args[-1], str(path.resolve()))

    def test_rejects_explicit_vendor_paths(self) -> None:
        vendor = self.root / "vendor" / "unit.py"
        vendor.parent.mkdir()
        vendor.write_text("vendor", encoding="utf-8")
        self.data["targets"]["python"] = ["vendor/unit.py"]
        self.save()
        with self.assertRaises(style.StyleError):
            self.load()

    def test_cpp_check_requires_repository_configuration(self) -> None:
        (self.root / ".clang-format").unlink()
        with self.assertRaisesRegex(style.StyleError, "required"):
            style.run("cpp", "check", [self.cpp], self.root)

    def test_private_configuration_and_generated_assets_are_never_targets(self) -> None:
        for filename in ("ble_auth.local.h", "nanoui_assets.cpp", "nanoui_font_provider.cpp"):
            with self.subTest(filename=filename):
                self.assertFalse(style._valid_source(Path("src") / filename, "cpp"))

    def test_prerelease_versions_do_not_match_release_pins(self) -> None:
        from subprocess import CompletedProcess

        for language, version in (
            ("cpp", "clang-format version 19.1.1rc1"),
            ("dart", "Dart SDK version: 3.11.4-dev.1.0"),
        ):
            with (
                self.subTest(language=language),
                patch.object(style, "resolve_tool", return_value="tool"),
            ):
                with self.assertRaisesRegex(style.StyleError, "must report"):
                    style.run(
                        language,
                        "check",
                        [],
                        self.root,
                        runner=lambda *args, **kwargs: CompletedProcess([], 0, version, ""),
                    )

    def test_generated_dart_is_rejected_explicitly_and_skipped_recursively(self) -> None:
        generated = self.root / "app" / "models.g.dart"
        generated.write_text("generated", encoding="utf-8")
        self.data["targets"]["dart"] = ["app/models.g.dart"]
        self.save()
        with self.assertRaises(style.StyleError):
            self.load()
        self.data["targets"]["dart"] = ["app"]
        self.save()
        self.assertEqual(self.load()["dart"], [self.dart.resolve()])

    def test_repository_parent_named_resources_does_not_exclude_sources(self) -> None:
        nested = self.root / "resources" / "crabit"
        nested.mkdir(parents=True)
        targets = {"cpp": ["unit.cpp"], "python": ["unit.py"], "dart": ["unit.dart"]}
        for filenames in targets.values():
            for filename in filenames:
                (nested / filename).write_text("content", encoding="utf-8")
        data = {"schema_version": 1, "versions": style.VERSIONS, "targets": targets}
        manifest = nested / "scope.json"
        manifest.write_text(json.dumps(data), encoding="utf-8")
        actual = style.load_targets(nested, manifest)
        self.assertEqual(actual["python"], [(nested / "unit.py").resolve()])

    def test_large_scope_is_batched_instead_of_one_windows_command(self) -> None:
        from subprocess import CompletedProcess

        calls = []

        def runner(argv, **kwargs):
            calls.append(argv)
            return CompletedProcess(argv, 0, stdout="black, 24.4.2", stderr="")

        files = [self.root / f"unit_{index}.py" for index in range(35)]
        with patch.object(style, "resolve_tool", return_value="black"):
            style.run("python", "check", files, self.root, runner)
        self.assertEqual(len(calls), 4)  # One version probe, three batches.
        selected = [
            argument for command in calls[1:] for argument in command if argument.endswith(".py")
        ]
        self.assertEqual(selected, [str(path) for path in files])

    def test_windows_batch_arguments_cannot_be_shell_syntax(self) -> None:
        with patch.object(style.os, "name", "nt"):
            for name in ("unsafe&file.dart", "unsafe%PATH%.dart", "unsafe\nfile.dart"):
                with self.subTest(path=name), self.assertRaises(style.StyleError):
                    style._argv("dart.bat", ["format", name])

    def test_recursive_symlink_cannot_select_outside_repository(self) -> None:
        with tempfile.TemporaryDirectory() as outside:
            external = Path(outside) / "external.py"
            external.write_text("external", encoding="utf-8")
            link = self.root / "src" / "external.py"
            try:
                link.symlink_to(external)
            except OSError:
                self.skipTest("OS does not permit creating symlinks")
            self.data["targets"]["python"] = ["src"]
            self.save()
            with self.assertRaises(style.StyleError):
                self.load()


if __name__ == "__main__":
    unittest.main()
