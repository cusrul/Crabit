"""Build a provenance-checked, explicitly non-release candidate bundle.

Inputs are explicit JSON objects. The artifact map has schema_version=1 and an
artifacts array; each item supplies kind, source_path, target, full source_sha,
sha256, and acceptance_status. Windows files use kind=windows-release-file;
BW16 images use kind=bw16-image. The provenance object has schema_version=1,
source_sha, code_tree_equivalent={verified, method}, and an artifacts mapping
keyed by the exact target with source_sha, sha256, and acceptance_status.

Example invocations should use a sanitized, clean handoff clone and a new output
directory. This tool validates recorded facts and checksums, but cannot attest
that caller-provided acceptance or code-equivalence statements are true.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile


FULL_SHA_RE = re.compile(r"^[0-9a-fA-F]{40}$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
WINDOWS_ROOT_FILES = {"native_assets.json"}
WINDOWS_ROOT_SUFFIXES = {".exe", ".dll"}
EXCLUDED_PARTS = {".git", ".ai", ".codex", "build", ".dart_tool", ".cache"}
EXCLUDED_NAMES = {
    "agents.md", "ble_auth.local.h", "project.env", ".env",
    "cookies", "cookie", "auth.json", "credentials.json",
}
ALLOWED_ACCEPTANCE_STATUSES = {
    "user-hardware-accepted", "compile-only", "not-hardware-accepted", "not-accepted", "unknown",
}
EXCLUDED_SUFFIXES = {".pdb", ".log", ".dmp", ".cache"}


def _fail(message: str) -> None:
    raise ValueError(message)


def _is_local_auth_header(name: str) -> bool:
    folded = name.casefold()
    if name == "ble_auth.local.example.h":
        return False
    return (folded.startswith("ble_auth") and ".h" in folded) or (
        "auth" in folded and ".local." in folded and ".h" in folded
    )


def _json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        _fail(f"Cannot read JSON input {path.name}: {exc}")
    if not isinstance(value, dict):
        _fail(f"JSON input must be an object: {path.name}")
    return value


def _safe_relative(value: str, label: str) -> PurePosixPath:
    if not isinstance(value, str) or not value or "\x00" in value:
        _fail(f"Invalid {label}")
    normalized = value.replace("\\", "/")
    path = PurePosixPath(normalized)
    if path.is_absolute() or re.match(r"^[A-Za-z]:", normalized):
        _fail(f"Absolute {label} is not allowed: {value}")
    if any(part in ("", ".", "..") for part in normalized.split("/")):
        _fail(f"Unsafe {label}: {value}")
    if any(part.casefold() in EXCLUDED_PARTS for part in path.parts):
        _fail(f"Excluded/private path in {label}: {value}")
    if (
        path.name.casefold() in EXCLUDED_NAMES
        or _is_local_auth_header(path.name)
        or path.suffix.casefold() in EXCLUDED_SUFFIXES
    ):
        _fail(f"Excluded/private filename in {label}: {value}")
    if any(part.casefold().endswith(".webview2") for part in path.parts):
        _fail(f"WebView2 runtime profile is not distributable: {value}")
    return path


def _safe_target(value: str) -> PurePosixPath:
    path = _safe_relative(value, "package target")
    if path.parts[0].casefold() in {".git", ".ai", ".codex", "build"}:
        _fail(f"Internal package target is not allowed: {value}")
    if path.parts[0].casefold() in {"source", "licenses"} or path.as_posix().casefold() in {
        "manifest.json", "candidate.zip",
    }:
        _fail(f"Reserved package target: {value}")
    return path


def _no_symlink_path(path: Path) -> None:
    absolute = Path(os.path.abspath(path))
    cursor = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        cursor = cursor / part
        if cursor.is_symlink():
            _fail(f"Symlinks are not allowed: {cursor}")


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _git(source: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), *args], check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    if result.returncode:
        _fail(f"Git validation failed ({args[0]}): {result.stderr.strip()}")
    return result.stdout.strip()


def _source_archive(source: Path, revision: str) -> tuple[bytes, list[dict]]:
    result = subprocess.run(
        ["git", "-C", str(source), "archive", "--format=tar", revision],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if result.returncode:
        _fail("git archive failed for the fixed source SHA")
    archive_members: list[tuple[str, bytes]] = []
    try:
        with tarfile.open(fileobj=io.BytesIO(result.stdout), mode="r:") as source_tar:
            for item in source_tar:
                if not item.isfile():
                    continue
                name = item.name.replace("\\", "/")
                raw_parts = PurePosixPath(name).parts
                if (
                    any(part.casefold() in EXCLUDED_PARTS for part in raw_parts)
                    or any(
                        part.casefold() in EXCLUDED_NAMES
                        or _is_local_auth_header(part)
                        for part in raw_parts
                    )
                    or PurePosixPath(name).suffix.casefold() in EXCLUDED_SUFFIXES
                    or any(part.casefold().endswith(".webview2") for part in raw_parts)
                ):
                    continue
                safe = _safe_relative(name, "source archive path")
                fileobj = source_tar.extractfile(item)
                if fileobj is None:
                    _fail(f"Cannot read archived source member: {name}")
                archive_members.append((safe.as_posix(), fileobj.read()))
    except (tarfile.TarError, OSError) as exc:
        _fail(f"Invalid git archive: {exc}")

    output = io.BytesIO()
    hashes = []
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for name, data in sorted(archive_members):
            bundle.writestr(name, data)
            hashes.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    return output.getvalue(), hashes


def _refuse_tracked_auth_headers(source: Path, revision: str) -> None:
    names = _git(source, "ls-tree", "-r", "--name-only", revision).splitlines()
    unsafe = [
        name for name in names
        if _is_local_auth_header(PurePosixPath(name).name)
    ]
    if unsafe:
        _fail("Fixed source tree tracks a local BLE auth header; refusing before git archive")


def _copy_licenses(source_zip: Path, stage: Path) -> None:
    license_names = ("license", "licence", "copying", "notice", "third_party_notices", "ofl")
    with zipfile.ZipFile(source_zip, "r") as archive:
        for name in archive.namelist():
            basename = PurePosixPath(name).name.casefold()
            if any(token in basename for token in license_names):
                target = stage / "licenses" / "source" / Path(*PurePosixPath(name).parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(name))


def _validate_provenance(provenance: dict, source_sha: str, artifacts: list[dict]) -> dict:
    if provenance.get("schema_version") != 1:
        _fail("Unsupported provenance schema_version (expected 1)")
    if provenance.get("source_sha", "").lower() != source_sha.lower():
        _fail("Provenance source_sha does not match --source-sha")
    equivalence = provenance.get("code_tree_equivalent")
    if not isinstance(equivalence, dict) or not isinstance(equivalence.get("verified"), bool):
        _fail("Provenance must contain caller-supplied code_tree_equivalent.verified")
    if not isinstance(equivalence.get("method"), str) or not equivalence["method"].strip():
        _fail("Provenance must describe the caller's code-tree equivalence method")
    recorded = provenance.get("artifacts")
    if not isinstance(recorded, dict):
        _fail("Provenance artifacts must map every package target")
    expected_targets = {item["target"] for item in artifacts}
    if set(recorded) != expected_targets:
        _fail("Artifact-map targets and provenance artifact targets must match exactly")
    for item in artifacts:
        entry = recorded[item["target"]]
        if not isinstance(entry, dict):
            _fail(f"Invalid provenance for {item['target']}")
        status = item.get("acceptance_status")
        if not isinstance(status, str) or status not in ALLOWED_ACCEPTANCE_STATUSES:
            _fail(f"Unknown or placeholder acceptance_status for {item['target']}")
        for key in ("source_sha", "sha256", "acceptance_status"):
            if str(entry.get(key, "")).lower() != str(item.get(key, "")).lower():
                _fail(f"Artifact-map {key} mismatch with provenance: {item['target']}")
    return equivalence


def _validate_windows_target(target: PurePosixPath) -> None:
    if not target.parts or target.parts[0].casefold() != "windows":
        _fail("Windows Release files must be placed under windows/")
    relative = target.parts[1:]
    if not relative:
        _fail("Windows Release target must name a file")
    if len(relative) == 1:
        name = relative[0].casefold()
        if name not in WINDOWS_ROOT_FILES and Path(name).suffix not in WINDOWS_ROOT_SUFFIXES:
            _fail(f"Windows Release root file is not allowlisted: {target}")
    elif relative[0].casefold() != "data":
        _fail(f"Only Windows root binaries/metadata and data/** are allowed: {target}")
    if any(part.casefold().endswith(".webview2") for part in relative):
        _fail("WebView2 runtime profile is excluded")
    if any(part.casefold() in {"cache", "caches", "logs", "log"} for part in relative):
        _fail("Windows cache/log content is excluded")


def _artifact_bytes(item: dict, source_root: Path) -> bytes:
    if item.get("kind") not in {"file", "windows-release-file", "bw16-image"}:
        _fail(f"Unsupported artifact kind for {item['target']}")
    if not FULL_SHA_RE.fullmatch(str(item.get("source_sha", ""))):
        _fail(f"Artifact source_sha must be a full 40-character commit: {item['target']}")
    if not SHA256_RE.fullmatch(str(item.get("sha256", ""))):
        _fail(f"Artifact sha256 must be a full SHA256: {item['target']}")
    target = _safe_target(item.get("target"))
    source_value = item.get("source_path")
    if not isinstance(source_value, str) or not source_value:
        _fail(f"Artifact source_path is required: {target}")
    source_path = Path(source_value)
    if not source_path.is_absolute():
        source_path = source_root / source_path
    _no_symlink_path(source_path)
    if not source_path.is_file():
        _fail(f"Artifact source is not a regular file: {target}")
    if source_path.name.casefold() in EXCLUDED_NAMES:
        _fail(f"Private/auth configuration cannot be an artifact: {target}")
    if _is_local_auth_header(source_path.name):
        _fail(f"BLE auth headers are never accepted as artifacts: {target}")

    if item["kind"] == "windows-release-file":
        _validate_windows_target(target)
        source_parts = [part.casefold() for part in source_path.parts]
        if any(part.endswith(".webview2") or part in {"cache", "caches", "logs", "log"} for part in source_parts):
            _fail(f"Windows runtime profile/cache/log source is excluded: {target}")
        if source_path.suffix.casefold() in EXCLUDED_SUFFIXES:
            _fail(f"Windows log/cache/PDB source is excluded: {target}")
    if item["kind"] == "bw16-image":
        if "bw16" not in "/".join(target.parts).casefold():
            _fail("BW16 image target must identify bw16")
        marker_found = any((parent / "DO_NOT_FLASH.txt").is_file() for parent in [source_path.parent, *source_path.parents])
        compile_only = item.get("acceptance_status") == "compile-only"
        do_not_flash = item.get("do_not_flash") is True
        in_compile_checks = target.parts[0].casefold() == "compile-checks"
        if compile_only:
            if not do_not_flash or not in_compile_checks:
                _fail("Compile-only BW16 images require do_not_flash=true under compile-checks/")
        elif do_not_flash or marker_found:
            _fail("DO_NOT_FLASH BW16 images cannot be packaged as flashable artifacts")
    elif item.get("do_not_flash") is True:
        _fail("do_not_flash applies only to BW16 compile-check images")

    data = source_path.read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual.lower() != item["sha256"].lower():
        _fail(f"Input artifact SHA256 mismatch: {target}")
    return data


def package_candidate(
    source_repo: Path, source_sha: str, provenance_path: Path,
    artifact_map_path: Path, output_dir: Path,
) -> dict:
    _no_symlink_path(source_repo)
    source = source_repo.resolve(strict=True)
    if not source.is_dir() or not (source / ".git").exists():
        _fail("--source-repo must be a Git working tree")
    if not FULL_SHA_RE.fullmatch(source_sha):
        _fail("--source-sha must be a full 40-character commit SHA")
    resolved_sha = _git(source, "rev-parse", "--verify", f"{source_sha}^{{commit}}")
    if resolved_sha.lower() != source_sha.lower():
        _fail("Resolved source commit does not equal the explicit full SHA")
    if _git(source, "status", "--porcelain", "--untracked-files=no"):
        _fail("Source repository has tracked modifications; refusing candidate archive")
    _refuse_tracked_auth_headers(source, source_sha)

    provenance = _json(provenance_path)
    artifact_map = _json(artifact_map_path)
    if artifact_map.get("schema_version") != 1 or not isinstance(artifact_map.get("artifacts"), list):
        _fail("Artifact map must use schema_version 1 and contain an artifacts array")
    artifacts = artifact_map["artifacts"]
    if not artifacts:
        _fail("At least one explicit artifact is required")
    targets = [_safe_target(item.get("target")) for item in artifacts if isinstance(item, dict)]
    if len(targets) != len(artifacts):
        _fail("Every artifact-map item must be an object")
    folded = [str(target).casefold() for target in targets]
    if len(folded) != len(set(folded)):
        _fail("Duplicate package target")
    marker_targets = {
        ("/".join(target.parts[:2]) + "/DO_NOT_FLASH.txt").casefold()
        for item, target in zip(artifacts, targets)
        if item.get("kind") == "bw16-image" and item.get("acceptance_status") == "compile-only"
    }
    if marker_targets.intersection(folded):
        _fail("Artifact target conflicts with generated DO_NOT_FLASH marker")
    equivalence = _validate_provenance(provenance, source_sha, artifacts)

    output = Path(os.path.abspath(output_dir))
    if output.is_symlink():
        _fail(f"Output path cannot be a symlink: {output}")
    if output.exists():
        _fail(f"Output already exists; refusing overwrite: {output}")
    _no_symlink_path(output.parent)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=f".{output.name}.candidate-", dir=output.parent))
    try:
        source_zip, source_files = _source_archive(source, source_sha)
        source_target = stage / "source" / "source.zip"
        source_target.parent.mkdir(parents=True, exist_ok=True)
        source_target.write_bytes(source_zip)
        _copy_licenses(source_target, stage)

        artifact_manifest = []
        for item, target in zip(artifacts, targets):
            data = _artifact_bytes(item, source)
            destination = stage.joinpath(*target.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            artifact_manifest.append({
                "target": target.as_posix(),
                "source_sha": item["source_sha"].lower(),
                "sha256": hashlib.sha256(data).hexdigest(),
                "bytes": len(data),
                "acceptance_status": item["acceptance_status"],
                "do_not_flash": item.get("do_not_flash", False),
            })

        compile_items = [x for x in artifact_manifest if x["do_not_flash"]]
        if compile_items:
            for target in sorted({"/".join(x["target"].split("/")[:2]) for x in compile_items}):
                marker = stage / target / "DO_NOT_FLASH.txt"
                marker.write_text(
                    "COMPILE CHECK ONLY. NOT HARDWARE ACCEPTED. DO NOT FLASH.\n",
                    encoding="ascii",
                )

        payload_files = []
        for path in sorted(p for p in stage.rglob("*") if p.is_file()):
            relative = path.relative_to(stage).as_posix()
            payload_files.append({
                "path": relative,
                "bytes": path.stat().st_size,
                "sha256": _sha256_file(path),
            })
        manifest = {
            "schema_version": 1,
            "package_status": "CANDIDATE_NOT_HARDWARE_ACCEPTED_NOT_PUBLIC_CLEARED",
            "public_release": False,
            "blanket_legal_clearance": False,
            "source": {
                "repository_role": "caller-designated handoff repository",
                "sha": source_sha.lower(),
                "archive_policy": "fixed git archive; internal/private path classes filtered",
            },
            "code_tree_equivalent": {
                "caller_asserted": True,
                "verified": equivalence["verified"],
                "method": equivalence["method"],
            },
            "artifacts": artifact_manifest,
            "source_archive_files": source_files,
            "files": payload_files,
            "limitations": [
                "Package assembly does not establish hardware acceptance.",
                "Package assembly does not establish public-release or license clearance.",
                "Artifact acceptance statuses are supplied by the caller and are not independently adjudicated.",
                "Source archive filtering is path-based, not a secret scan; caller must provide a sanitized handoff and review it.",
            ],
        }
        (stage / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8",
        )
        archive_path = stage / "candidate.zip"
        with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(p for p in stage.rglob("*") if p.is_file() and p != archive_path):
                archive.write(path, path.relative_to(stage).as_posix())
        _verify_roundtrip(stage, manifest)
        os.rename(stage, output)
        return {"output": str(output), "manifest": str(output / "manifest.json"), "archive": str(output / "candidate.zip")}
    except Exception:
        shutil.rmtree(stage, ignore_errors=True)
        raise


def _verify_roundtrip(root: Path, manifest: dict) -> None:
    expected = {item["path"]: item for item in manifest["files"]}
    expected["manifest.json"] = {
        "bytes": (root / "manifest.json").stat().st_size,
        "sha256": _sha256_file(root / "manifest.json"),
    }
    with zipfile.ZipFile(root / "candidate.zip", "r") as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or set(names) != set(expected):
            _fail("Candidate ZIP entry set differs from the verified package file set")
        for name, record in expected.items():
            _safe_relative(name, "ZIP member")
            data = archive.read(name)
            if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
                _fail(f"Candidate ZIP roundtrip hash mismatch: {name}")
    with zipfile.ZipFile(root / "source" / "source.zip", "r") as archive:
        for member in archive.namelist():
            _safe_relative(member, "source ZIP member")
            if member.casefold().endswith((".pdb", ".log")):
                _fail(f"Forbidden source archive file: {member}")
        actual_source_files = _source_zip_manifest(root / "source" / "source.zip")
        if actual_source_files != manifest["source_archive_files"]:
            _fail("Source archive contents differ from the recorded git-archive member manifest")
        with zipfile.ZipFile(root / "source" / "source.zip") as source_archive:
            for record in manifest["source_archive_files"]:
                data = source_archive.read(record["path"])
                if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
                    _fail(f"Source archive roundtrip hash mismatch: {record['path']}")


def _source_zip_manifest(path: Path) -> list[dict]:
    with zipfile.ZipFile(path, "r") as archive:
        return [
            {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            for name in archive.namelist()
            for data in [archive.read(name)]
        ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-repo", required=True, type=Path)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--provenance", required=True, type=Path)
    parser.add_argument("--artifact-map", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = package_candidate(args.source_repo, args.source_sha, args.provenance, args.artifact_map, args.output)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as exc:
        raise SystemExit(f"release candidate refused: {exc}")
