"""Fail-closed tests for the non-release candidate packager."""

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

from tools.package_release_candidate import package_candidate


class ReleaseCandidateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "handoff"
        self.repo.mkdir()
        self._git("init", "-q")
        self._git("config", "user.email", "test@example.invalid")
        self._git("config", "user.name", "Release Candidate Test")
        (self.repo / "README.md").write_text("sanitized handoff fixture\n", encoding="utf-8")
        (self.repo / "LICENSE").write_text("fixture license text\n", encoding="utf-8")
        (self.repo / "AGENTS.md").write_text("must not enter archive\n", encoding="utf-8")
        (self.repo / ".ai").mkdir()
        (self.repo / ".ai" / "work.md").write_text("internal\n", encoding="utf-8")
        self._git("add", "README.md", "LICENSE", "AGENTS.md", ".ai/work.md")
        self._git("commit", "-qm", "fixture handoff")
        self.sha = self._git("rev-parse", "HEAD")
        self.artifacts_dir = self.root / "artifacts"
        self.artifacts_dir.mkdir()
        self.artifact_path = self.artifacts_dir / "board.elf"
        self.artifact_path.write_bytes(b"accepted fixture binary")
        self.artifact = {
            "kind": "file",
            "source_path": str(self.artifact_path),
            "target": "stm32/board.elf",
            "source_sha": self.sha,
            "sha256": self._hash(self.artifact_path),
            "acceptance_status": "user-hardware-accepted",
        }
        self.provenance_path = self.root / "provenance.json"
        self.map_path = self.root / "artifact-map.json"
        self.output = self.root / "out" / "candidate"
        self._inputs([self.artifact])

    def _git(self, *args):
        return subprocess.run(
            ["git", "-C", str(self.repo), *args], check=True,
            capture_output=True, text=True,
        ).stdout.strip()

    @staticmethod
    def _hash(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def _inputs(self, artifacts):
        self.map_path.write_text(json.dumps({"schema_version": 1, "artifacts": artifacts}), encoding="utf-8")
        self.provenance_path.write_text(json.dumps({
            "schema_version": 1,
            "source_sha": self.sha,
            "code_tree_equivalent": {"verified": True, "method": "caller compared selected source hashes"},
            "artifacts": {
                item["target"]: {
                    key: item.get(key)
                    for key in ("source_sha", "sha256", "acceptance_status")
                }
                for item in artifacts
            },
        }), encoding="utf-8")

    def _package(self):
        return package_candidate(self.repo, self.sha, self.provenance_path, self.map_path, self.output)

    def test_builds_candidate_archive_and_hash_manifest(self):
        result = self._package()
        manifest = json.loads(Path(result["manifest"]).read_text(encoding="utf-8"))
        self.assertEqual(manifest["package_status"], "CANDIDATE_NOT_HARDWARE_ACCEPTED_NOT_PUBLIC_CLEARED")
        self.assertFalse(manifest["public_release"])
        self.assertFalse(manifest["blanket_legal_clearance"])
        with zipfile.ZipFile(self.output / "source/source.zip") as archive:
            names = archive.namelist()
            self.assertIn("LICENSE", names)
            self.assertNotIn("AGENTS.md", names)
            self.assertNotIn(".ai/work.md", names)
        self.assertTrue((self.output / "licenses/source/LICENSE").is_file())
        with zipfile.ZipFile(self.output / "candidate.zip") as archive:
            self.assertIn("manifest.json", archive.namelist())
            self.assertIn("source/source.zip", archive.namelist())
        self.assertTrue(Path(result["archive"]).is_file())

    def test_output_is_never_overwritten(self):
        self._package()
        with self.assertRaisesRegex(ValueError, "already exists"):
            self._package()

    def test_unsafe_target_is_rejected(self):
        self.artifact["target"] = "../outside.elf"
        self._inputs([self.artifact])
        with self.assertRaisesRegex(ValueError, "Unsafe package target"):
            self._package()

    def test_tampered_input_hash_is_rejected(self):
        self.artifact_path.write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            self._package()

    def test_map_provenance_hash_mismatch_is_rejected(self):
        self._inputs([self.artifact])
        provenance = json.loads(self.provenance_path.read_text(encoding="utf-8"))
        provenance["artifacts"][self.artifact["target"]]["sha256"] = "0" * 64
        self.provenance_path.write_text(json.dumps(provenance), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Artifact-map sha256 mismatch"):
            self._package()

    def test_placeholder_status_is_rejected(self):
        self.artifact["acceptance_status"] = "REPLACE"
        self._inputs([self.artifact])
        with self.assertRaisesRegex(ValueError, "placeholder acceptance_status"):
            self._package()

    def test_dirty_tracked_source_is_rejected(self):
        (self.repo / "README.md").write_text("changed\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "tracked modifications"):
            self._package()

    def test_tracked_auth_header_is_refused_before_archive(self):
        header = self.repo / "firmware/NanoUI/ble_auth.local.h"
        header.parent.mkdir(parents=True)
        header.write_text("must not be read\n", encoding="ascii")
        self._git("add", "firmware/NanoUI/ble_auth.local.h")
        self._git("commit", "-qm", "fixture local auth header")
        self.sha = self._git("rev-parse", "HEAD")
        self.artifact["source_sha"] = self.sha
        self._inputs([self.artifact])
        with self.assertRaisesRegex(ValueError, "tracks a local BLE auth header"):
            self._package()

    def test_compile_only_bw16_requires_compile_checks_and_marker(self):
        image_dir = self.artifacts_dir / "bw16"
        image_dir.mkdir()
        image = image_dir / "km0_km4_image2.bin"
        image.write_bytes(b"compile-only image")
        (image_dir / "DO_NOT_FLASH.txt").write_text("compile only\n", encoding="ascii")
        item = {
            "kind": "bw16-image", "source_path": str(image),
            "target": "firmware/bw16/km0_km4_image2.bin", "source_sha": self.sha,
            "sha256": self._hash(image), "acceptance_status": "compile-only", "do_not_flash": True,
        }
        self._inputs([item])
        with self.assertRaisesRegex(ValueError, "under compile-checks"):
            self._package()
        item["target"] = "compile-checks/bw16/km0_km4_image2.bin"
        self._inputs([item])
        self._package()
        marker = self.output / "compile-checks/bw16/DO_NOT_FLASH.txt"
        self.assertIn("DO NOT FLASH", marker.read_text(encoding="ascii"))

    def test_do_not_flash_source_cannot_be_labelled_flashable(self):
        image_dir = self.artifacts_dir / "bw16"
        image_dir.mkdir()
        image = image_dir / "image.bin"
        image.write_bytes(b"not for flash")
        (image_dir / "DO_NOT_FLASH.txt").write_text("no\n", encoding="ascii")
        item = {
            "kind": "bw16-image", "source_path": str(image),
            "target": "firmware/bw16/image.bin", "source_sha": self.sha,
            "sha256": self._hash(image), "acceptance_status": "user-hardware-accepted", "do_not_flash": False,
        }
        self._inputs([item])
        with self.assertRaisesRegex(ValueError, "cannot be packaged as flashable"):
            self._package()

    def test_windows_cache_runtime_log_and_pdb_targets_are_rejected(self):
        for target in (
            "windows/data/cache/state.bin",
            "windows/crabit_watch_app.exe.WebView2/profile/data.bin",
            "windows/data/app.pdb",
            "windows/app.log",
        ):
            with self.subTest(target=target):
                item = dict(self.artifact, kind="windows-release-file", target=target)
                self._inputs([item])
                with self.assertRaises(ValueError):
                    self._package()

    def test_windows_nonallowlisted_root_file_is_rejected(self):
        item = dict(self.artifact, kind="windows-release-file", target="windows/notes.txt")
        self._inputs([item])
        with self.assertRaisesRegex(ValueError, "not allowlisted"):
            self._package()

    def test_windows_source_runtime_profile_is_rejected_even_with_safe_target(self):
        runtime = self.artifacts_dir / "profile.WebView2" / "app.exe"
        runtime.parent.mkdir()
        runtime.write_bytes(b"runtime profile")
        item = dict(
            self.artifact, kind="windows-release-file", source_path=str(runtime),
            target="windows/crabit_watch_app.exe", sha256=self._hash(runtime),
        )
        self._inputs([item])
        with self.assertRaisesRegex(ValueError, "runtime profile/cache/log source"):
            self._package()

    def test_windows_release_allowlist_accepts_exe_and_data(self):
        exe = dict(self.artifact, kind="windows-release-file", target="windows/crabit_watch_app.exe")
        asset_path = self.artifacts_dir / "asset.bin"
        asset_path.write_bytes(b"app asset")
        asset = dict(
            exe, source_path=str(asset_path), target="windows/data/flutter_assets/asset.bin",
            sha256=self._hash(asset_path),
        )
        self._inputs([exe, asset])
        self._package()
        self.assertTrue((self.output / "windows/crabit_watch_app.exe").is_file())
        self.assertTrue((self.output / "windows/data/flutter_assets/asset.bin").is_file())

    def test_symlink_artifact_is_rejected(self):
        link = self.artifacts_dir / "linked.elf"
        try:
            link.symlink_to(self.artifact_path)
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation unavailable")
        self.artifact["source_path"] = str(link)
        self._inputs([self.artifact])
        with self.assertRaisesRegex(ValueError, "Symlinks are not allowed"):
            self._package()


if __name__ == "__main__":
    unittest.main()
