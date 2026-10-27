from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.collect_flutter_notices import CollectionError, collect


class CollectFlutterNoticesTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="flutter-notices-")
        self.root = Path(self.temp.name)
        self.cache = self.root / "pub-cache" / "hosted" / "pub.dev"
        self.cache.mkdir(parents=True)
        self.app = self.root / "app" / "apps" / "mobile"
        self.app.mkdir(parents=True)
        self.config_path = self.app / ".dart_tool" / "package_config.json"
        self.config_path.parent.mkdir()
        self.lock_path = self.app / "pubspec.lock"
        self.output = self.root / "collected-notices"

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _package(self, name: str, version: str, files: dict[str, bytes] | None = None) -> Path:
        package = self.cache / f"{name}-{version}"
        package.mkdir(parents=True)
        for filename, data in (files or {}).items():
            target = package / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        return package

    def _inputs(self, package_roots: dict[str, Path], lock: str) -> None:
        packages = [
            {"name": name, "rootUri": root.as_uri() + "/", "packageUri": "lib/"}
            for name, root in package_roots.items()
        ]
        self.config_path.write_text(json.dumps({"configVersion": 2, "packages": packages}), encoding="utf-8")
        self.lock_path.write_text(lock, encoding="utf-8")

    def test_copies_exact_root_notice_bytes_and_emits_private_path_free_manifest(self) -> None:
        fbp = self._package(
            "flutter_blue_plus", "2.2.1",
            {"LICENSE": b"commercial terms\r\n\x00exact", "NOTICE.md": b"BSD contributor notice\n", "sub/LICENSE": b"not root"},
        )
        winrt = self._package("flutter_blue_plus_winrt", "0.0.18", {"LICENSE": b"winrt MIT\r\n"})
        sdk = self.root / "flutter-sdk" / "packages" / "flutter"
        sdk.mkdir(parents=True)
        (sdk / "LICENSE").write_bytes(b"flutter sdk license\n")
        app_root = self.app
        self._inputs(
            {"flutter_blue_plus": fbp, "flutter_blue_plus_winrt": winrt, "flutter": sdk, "my_app": app_root},
            """packages:
  flutter_blue_plus:
    dependency: direct main
    description:
      name: flutter_blue_plus
      url: https://pub.dev
    source: hosted
    version: \"2.2.1\"
  flutter_blue_plus_winrt:
    dependency: transitive
    description:
      name: flutter_blue_plus_winrt
      url: https://pub.dev
    source: hosted
    version: \"0.0.18\"
  flutter:
    dependency: direct main
    description: flutter
    source: sdk
    version: \"0.0.0\"
""",
        )

        manifest = collect(self.config_path, self.lock_path, self.output)
        fbp_record = next(item for item in manifest["packages"] if item["name"] == "flutter_blue_plus")
        self.assertEqual(fbp_record["version"], "2.2.1")
        self.assertEqual(fbp_record["resolution"], "locked")
        self.assertEqual(fbp_record["registry_url"], "https://pub.dev")
        self.assertNotIn("upstream_url", fbp_record)
        self.assertEqual(fbp_record["review_flags"], ["commercial-license-review"])
        self.assertEqual(len(fbp_record["license_files"]), 2)
        self.assertEqual(
            (self.output / fbp_record["license_files"][0]["file"]).read_bytes(),
            b"commercial terms\r\n\x00exact",
        )
        self.assertEqual(
            fbp_record["license_files"][0]["sha256"],
            hashlib.sha256(b"commercial terms\r\n\x00exact").hexdigest(),
        )

        winrt_record = next(item for item in manifest["packages"] if item["name"] == "flutter_blue_plus_winrt")
        self.assertNotIn("review_flags", winrt_record)
        self.assertEqual((self.output / winrt_record["license_files"][0]["file"]).read_bytes(), b"winrt MIT\r\n")
        sdk_record = next(item for item in manifest["packages"] if item["name"] == "flutter")
        self.assertEqual(sdk_record["resolution"], "sdk")
        self.assertEqual(sdk_record["version"], "0.0.0")
        app_record = next(item for item in manifest["packages"] if item["name"] == "my_app")
        self.assertEqual(app_record["resolution"], "root")
        self.assertEqual(app_record["notice_status"], "application-root-not-collected")

        serialized = (self.output / "manifest.json").read_text(encoding="utf-8")
        self.assertNotIn(str(self.root), serialized)
        self.assertNotIn(str(self.cache), serialized)
        self.assertFalse((self.output / "packages" / "flutter_blue_plus-2.2.1" / "sub").exists())

    def test_missing_notice_list_includes_missing_and_symlink_only_packages(self) -> None:
        missing = self._package("missing_notice", "1.0.0")
        symlink_only = self._package("symlink_notice", "1.0.0")
        outside = self.root / "outside-license"
        outside.write_bytes(b"must not follow")
        try:
            (symlink_only / "LICENSE.txt").symlink_to(outside)
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation is unavailable")
        self._inputs(
            {"missing_notice": missing, "symlink_notice": symlink_only},
            """packages:
  missing_notice:
    dependency: transitive
    description:
      name: missing_notice
      url: https://pub.dev
    source: hosted
    version: 1.0.0
  symlink_notice:
    dependency: transitive
    description:
      name: symlink_notice
      url: https://pub.dev
    source: hosted
    version: 1.0.0
""",
        )

        manifest = collect(self.config_path, self.lock_path, self.output)
        self.assertEqual(
            {item["name"]: item["reason"] for item in manifest["missing_notices"]},
            {
                "missing_notice": "no-root-license-or-notice-files",
                "symlink_notice": "matching-notice-files-are-symlinks",
            },
        )
        self.assertEqual(json.loads((self.output / "missing-notices.json").read_text()), manifest["missing_notices"])
        self.assertFalse(any(path.is_symlink() for path in (self.output / "packages").rglob("*")))

    def test_git_source_url_is_redacted_and_lock_version_is_authoritative(self) -> None:
        package = self._package("git_dependency", "3.4.5", {"COPYING": b"copy"})
        self._inputs(
            {"git_dependency": package},
            """packages:
  git_dependency:
    dependency: transitive
    description:
      url: https://user:secret@example.com/org/repo.git?token=private
      ref: main
    source: git
    version: \"3.4.5\"
""",
        )
        manifest = collect(self.config_path, self.lock_path, self.output)
        record = manifest["packages"][0]
        self.assertEqual(record["version"], "3.4.5")
        self.assertEqual(record["upstream_url"], "https://example.com/org/repo.git")
        self.assertNotIn("secret", (self.output / "manifest.json").read_text())
        self.assertNotIn("private", (self.output / "manifest.json").read_text())

    def test_pre_switch_major_versions_are_not_mislabeled_commercial(self) -> None:
        main = self._package("flutter_blue_plus", "1.36.8", {"LICENSE": b"BSD-3-Clause"})
        interface = self._package("flutter_blue_plus_platform_interface", "7.0.0", {"LICENSE": b"BSD-3-Clause"})
        self._inputs(
            {"flutter_blue_plus": main, "flutter_blue_plus_platform_interface": interface},
            """packages:
  flutter_blue_plus:
    dependency: direct main
    description:
      url: https://pub.dev
    source: hosted
    version: 1.36.8
  flutter_blue_plus_platform_interface:
    dependency: transitive
    description:
      url: https://pub.dev
    source: hosted
    version: 7.0.0
""",
        )
        manifest = collect(self.config_path, self.lock_path, self.output)
        self.assertEqual(manifest["review_flags"], [])
        self.assertTrue(all("review_flags" not in item for item in manifest["packages"]))

    def test_underscore_leading_lock_package_is_locked_not_misclassified_as_root(self) -> None:
        package = self._package("_fe_analyzer_shared", "91.0.0", {"LICENSE": b"license"})
        self._inputs(
            {"_fe_analyzer_shared": package, "my_app": self.app},
            """packages:
  _fe_analyzer_shared:
    dependency: transitive
    description:
      name: _fe_analyzer_shared
      url: https://pub.dev
    source: hosted
    version: \"91.0.0\"
""",
        )
        manifest = collect(self.config_path, self.lock_path, self.output)
        record = next(item for item in manifest["packages"] if item["name"] == "_fe_analyzer_shared")
        self.assertEqual(record["version"], "91.0.0")
        self.assertEqual(record["resolution"], "locked")
        self.assertEqual(record["notice_status"], "collected")
        root_record = next(item for item in manifest["packages"] if item["name"] == "my_app")
        self.assertEqual(root_record["resolution"], "root")

    def test_unmatched_nonroot_package_config_entry_fails_instead_of_becoming_root(self) -> None:
        package = self._package("_fe_analyzer_shared", "91.0.0", {"LICENSE": b"license"})
        self._inputs({"_fe_analyzer_shared": package}, "packages:\n")
        with self.assertRaisesRegex(CollectionError, "no parsed lockfile entry"):
            collect(self.config_path, self.lock_path, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_existing_output_cache_destination_and_malformed_inputs(self) -> None:
        package_a = self._package("one", "1.0.0", {"LICENSE": b"one"})
        package_b = self._package("two", "2.0.0", {"LICENSE": b"two"})
        self._inputs(
            {"one": package_a, "two": package_b},
            """packages:
  one:
    dependency: transitive
    description: {name: one}
    source: hosted
    version: 1.0.0
  two:
    dependency: transitive
    description: {name: two}
    source: hosted
    version: 2.0.0
""",
        )
        self.output.mkdir()
        with self.assertRaisesRegex(CollectionError, "new directory"):
            collect(self.config_path, self.lock_path, self.output)
        self.output.rmdir()

        with self.assertRaisesRegex(CollectionError, "package cache"):
            collect(self.config_path, self.lock_path, self.cache / "new-output")

        self.config_path.write_text('{"packages":[{"name":"../escape","rootUri":"../"}]}', encoding="utf-8")
        with self.assertRaisesRegex(CollectionError, "unsafe package name"):
            collect(self.config_path, self.lock_path, self.output)

        self.config_path.write_text("not json", encoding="utf-8")
        with self.assertRaisesRegex(CollectionError, "package_config.json"):
            collect(self.config_path, self.lock_path, self.output)


if __name__ == "__main__":
    unittest.main()
