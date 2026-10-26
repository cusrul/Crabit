"""Check the recorded Altium import without changing CAD data or needing AD."""

import argparse
import hashlib
import json
import ntpath
from pathlib import Path
import re


def inside(root: Path, name: str) -> Path:
    normalized = name.replace("\\", "/")
    if ntpath.isabs(name) or ":" in name or ".." in normalized.split("/"):
        raise ValueError(f"Non-portable path: {name}")
    path = (root / normalized).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"Path escapes package: {name}")
    return path


def verify(root: Path) -> dict:
    report = json.loads((root / "IMPORT-REPORT.json").read_text(encoding="utf-8"))
    seen = set()
    for item in report["copied_binary_files"]:
        name = item["file"]
        if name.casefold() in seen:
            raise ValueError(f"Duplicate manifest entry: {name}")
        seen.add(name.casefold())
        data = inside(root, name).read_bytes()
        if len(data) != item["bytes"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
            raise ValueError(f"Binary content differs from package manifest: {name}")
    project = inside(root, report["project_file"])
    data = project.read_bytes()
    if hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest() != report["project_sha256"]:
        raise ValueError("Project configuration changed after recorded import")
    text = data.decode("utf-8-sig")
    if re.search(r"[A-Za-z]:[\\/]", text):
        raise ValueError("Project still contains an absolute machine path")
    if re.search(r"^\[GeneratedDocument\d+\]", text, re.M):
        raise ValueError("Historical generated documents remain in project")
    refs = re.findall(r"(?:^|\|)DocumentPath=([^|\r\n]+)", text, re.M)
    if not refs:
        raise ValueError("Project has no document references")
    for name in refs:
        if not inside(root, name.strip()).is_file():
            raise ValueError(f"Unresolved project reference: {name}")
    return {"binary_files": len(seen), "document_references": len(refs)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "hardware/altium/crabit-v2")
    args = parser.parse_args()
    result = verify(args.directory)
    print(f"PASS: {result['binary_files']} binary hashes, {result['document_references']} local references.")
    print("This is file/path validation, not an Altium compile, DRC or manufacturing acceptance.")
