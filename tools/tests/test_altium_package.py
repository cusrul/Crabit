import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.verify_altium_package import verify


class AltiumPackageChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "libraries").mkdir()
        files = []
        for name in ["board.PcbDoc", "libraries/example.PcbLib"]:
            data = b"fixture\x00" + name.encode()
            (self.root / name).write_bytes(data)
            files.append({"file": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        self.report = {"copied_binary_files": files, "project_file": "fixture.PrjPcb"}
        self.project("[Document1]\nDocumentPath=board.PcbDoc\nConfiguration=Print|DocumentPath=libraries\\example.PcbLib\n")

    def project(self, text):
        data = text.encode("utf-8-sig")
        (self.root / "fixture.PrjPcb").write_bytes(data)
        self.report["project_sha256"] = hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()
        (self.root / "IMPORT-REPORT.json").write_text(json.dumps(self.report), encoding="utf-8")

    def test_valid_package(self):
        self.assertEqual(verify(self.root), {"binary_files": 2, "document_references": 2})

    def test_changed_library_fails(self):
        (self.root / "libraries/example.PcbLib").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "Binary content differs"):
            verify(self.root)

    def test_git_archive_and_checkout_line_endings(self):
        project = self.root / "fixture.PrjPcb"
        project.write_bytes(project.read_bytes().replace(b"\n", b"\r\n"))
        self.assertEqual(verify(self.root)["binary_files"], 2)

    def test_parent_path_fails(self):
        self.project("[Document1]\nDocumentPath=..\\board.PcbDoc\n")
        with self.assertRaisesRegex(ValueError, "Non-portable"):
            verify(self.root)

    def test_embedded_absolute_target_fails(self):
        self.project("[Document1]\nDocumentPath=board.PcbDoc\nConfiguration=Print|DocumentPath=C:\\private\\board.PcbDoc\n")
        with self.assertRaisesRegex(ValueError, "absolute machine path"):
            verify(self.root)

    def test_stale_generated_document_fails(self):
        self.project("[Document1]\nDocumentPath=board.PcbDoc\n[GeneratedDocument1]\nDocumentPath=old.GTL\n")
        with self.assertRaisesRegex(ValueError, "Historical generated"):
            verify(self.root)


if __name__ == "__main__":
    unittest.main()
