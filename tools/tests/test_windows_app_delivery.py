"""Exercise the delivery verifier without launching the app or reading user data."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.name == "nt" and shutil.which("powershell"), "Windows PowerShell required")
class DeliveryVerifierTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="crabit-delivery-test-")
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.script = root / "tools/start-windows-app.ps1"
        self.script.parent.mkdir()
        shutil.copyfile(ROOT / "tools/start-windows-app.ps1", self.script)
        self.bundle = root / "build/delivery/windows/verified"
        self.bundle.mkdir(parents=True)
        self.app = self.bundle / "crabit_watch_app.exe"
        self.app.write_bytes(b"test fixture, not executable")
        self.entry = {"path": self.app.name, "sha256": hashlib.sha256(self.app.read_bytes()).hexdigest()}

    def verify(self, entries):
        (self.bundle / "bundle-manifest.json").write_text(json.dumps({"files": entries}), encoding="utf-8")
        return subprocess.run(
            ["powershell", "-NoProfile", "-File", str(self.script), "-VerifyOnly"],
            capture_output=True, text=True, timeout=30,
        )

    def test_valid_manifest(self):
        result = self.verify([self.entry])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_executable_must_be_in_manifest(self):
        self.assertNotEqual(self.verify([]).returncode, 0)

    def test_tampered_file(self):
        self.app.write_bytes(b"changed")
        self.assertNotEqual(self.verify([self.entry]).returncode, 0)

    def test_duplicate_file(self):
        self.assertNotEqual(self.verify([self.entry, self.entry]).returncode, 0)

    def test_unsafe_paths(self):
        for path in ("../outside", "sub/../../outside", r"sub\..\outside", r"\\server\file", "C:/file", "file:stream", ""):
            with self.subTest(path=path):
                self.assertNotEqual(self.verify([{**self.entry, "path": path}]).returncode, 0)


if __name__ == "__main__":
    unittest.main()
