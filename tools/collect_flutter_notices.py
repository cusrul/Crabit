#!/usr/bin/env python3
"""Collect Flutter/Dart package license and notice files from an installed graph.

The output is an inventory aid, not a license compatibility or distribution
clearance. Only root-level LICENSE*, COPYING*, and NOTICE* regular files are
copied, byte-for-byte. No package metadata or source files are copied.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import tempfile
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit, urlunsplit
from urllib.request import url2pathname


_PACKAGE_NAME = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]*$")
_LOCK_PACKAGE = re.compile(r"^  ([A-Za-z0-9_][A-Za-z0-9_.-]*):\s*$")
_LOCK_PACKAGE_LIKE = re.compile(r"^  ([^\s].*):\s*$")
_LOCK_FIELD = re.compile(r"^    ([a-z_]+):(?:\s+(.*))?$")
_LOCK_DESCRIPTION_FIELD = re.compile(r"^      ([a-z_]+):(?:\s+(.*))?$")
_SAFE_COMPONENT = re.compile(r"[^A-Za-z0-9._-]+")
_NOTICE_PREFIXES = ("license", "copying", "notice")
_COMMERCIAL_FAMILY_MAJORS = {
    "flutter_blue_plus": 2,
    "flutter_blue_plus_android": 8,
    "flutter_blue_plus_darwin": 8,
    "flutter_blue_plus_linux": 8,
    "flutter_blue_plus_platform_interface": 8,
    "flutter_blue_plus_web": 8,
}


class CollectionError(ValueError):
    """Raised when inputs or output paths are unsafe or malformed."""


def _scalar(value: str | None) -> str | None:
    """Parse the scalar forms used by pub's lockfile without a YAML dependency."""
    if value is None:
        return ""
    value = value.strip()
    if not value:
        return ""
    if value.startswith('"'):
        try:
            decoded = json.loads(value)
        except json.JSONDecodeError as error:
            raise CollectionError(f"invalid quoted lockfile scalar: {value!r}") from error
        if not isinstance(decoded, str):
            raise CollectionError(f"expected a string lockfile scalar: {value!r}")
        return decoded
    if value.startswith("'"):
        if not value.endswith("'") or len(value) < 2:
            raise CollectionError(f"invalid quoted lockfile scalar: {value!r}")
        return value[1:-1].replace("''", "'")
    return re.split(r"\s+#", value, maxsplit=1)[0].strip()


def parse_lockfile(path: Path) -> dict[str, dict[str, str | None]]:
    """Read package source/version and non-local source URLs from pubspec.lock."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise CollectionError(f"cannot read lockfile: {error}") from error

    packages: dict[str, dict[str, str | None]] = {}
    current: dict[str, str | None] | None = None
    in_description = False
    for line_number, line in enumerate(lines, 1):
        match = _LOCK_PACKAGE.match(line)
        if match:
            name = match.group(1)
            if name in packages:
                raise CollectionError(f"duplicate package {name!r} at lockfile line {line_number}")
            current = {"source": None, "version": None, "url": None}
            packages[name] = current
            in_description = False
            continue
        if _LOCK_PACKAGE_LIKE.match(line):
            raise CollectionError(f"invalid package header at lockfile line {line_number}")
        if current is None:
            continue
        if line.startswith("    ") and not line.startswith("      "):
            field = _LOCK_FIELD.match(line)
            if not field:
                in_description = False
                continue
            key, raw = field.groups()
            in_description = key == "description"
            if key in ("source", "version"):
                current[key] = _scalar(raw)
            continue
        if in_description and line.startswith("      "):
            field = _LOCK_DESCRIPTION_FIELD.match(line)
            if field and field.group(1) == "url":
                current["url"] = _scalar(field.group(2))
    return packages


def _safe_external_url(value: str | None) -> str | None:
    """Keep public URL identity while stripping credentials, query, and fragment."""
    if not value:
        return None
    scp_style = re.fullmatch(r"(?:[^@/:]+@)?([A-Za-z0-9.-]+):(.+)", value)
    if scp_style and "." in scp_style.group(1):
        host, path = scp_style.groups()
        return f"ssh://{host}/{quote(unquote(path), safe='/%:@+~!$&\'()*,-._;=')}"
    try:
        parsed = urlsplit(value)
        if parsed.scheme.lower() not in {"https", "http", "ssh", "git"} or not parsed.hostname:
            return None
        host = parsed.hostname
        if ":" in host and not host.startswith("["):
            host = f"[{host}]"
        netloc = host
        if parsed.port is not None:
            netloc = f"{netloc}:{parsed.port}"
        path = quote(unquote(parsed.path), safe="/%:@+~!$&'()*,-._;=")
        return urlunsplit((parsed.scheme.lower(), netloc, path, "", ""))
    except (ValueError, UnicodeError):
        return None


def _package_root(config_path: Path, root_uri: str) -> Path:
    parsed = urlsplit(root_uri)
    if parsed.scheme:
        if parsed.scheme.lower() != "file":
            raise CollectionError(f"unsupported package root URI scheme: {parsed.scheme}")
        raw_path = url2pathname(unquote(parsed.path))
        if parsed.netloc and parsed.netloc.lower() != "localhost":
            raw_path = f"//{parsed.netloc}{raw_path}"
        if os.name == "nt" and re.match(r"^\\[A-Za-z]:", raw_path):
            raw_path = raw_path[1:]
        return Path(raw_path)
    return config_path.parent / unquote(parsed.path)


def _is_symlink_path(path: Path) -> bool:
    """Detect symlinked path components without following them for collection."""
    absolute = Path(os.path.abspath(path))
    cursor = absolute
    while True:
        try:
            is_junction = getattr(cursor, "is_junction", None)
            if cursor.is_symlink() or (callable(is_junction) and is_junction()):
                return True
        except OSError:
            return True
        if cursor.parent == cursor:
            return False
        cursor = cursor.parent


def _safe_filename(name: str) -> str:
    safe = _SAFE_COMPONENT.sub("_", name).strip("._")
    if not safe:
        raise CollectionError(f"unsafe or empty notice filename: {name!r}")
    return safe


def _safe_package_component(value: str) -> str:
    safe = _SAFE_COMPONENT.sub("_", value).strip("._")
    if not safe or safe in {".", ".."}:
        raise CollectionError(f"unsafe package output component: {value!r}")
    return safe


def _commercial_review(name: str, version: str | None) -> bool:
    major_limit = _COMMERCIAL_FAMILY_MAJORS.get(name)
    if major_limit is None or version is None:
        return False
    match = re.match(r"^(\d+)(?:\.|-|\+|$)", version)
    return bool(match and int(match.group(1)) >= major_limit)


def _cache_roots(roots: list[Path]) -> set[Path]:
    """Infer package-cache roots from repeated package directories in one parent."""
    counts: dict[Path, int] = {}
    for root in roots:
        parent = root.resolve().parent
        counts[parent] = counts.get(parent, 0) + 1
    caches = {parent.resolve() for parent, count in counts.items() if count >= 2}
    for root in roots:
        physical_root = root.resolve()
        parts = [part.casefold() for part in physical_root.parts]
        for index, part in enumerate(parts):
            if part in {".pub-cache", "pub-cache"}:
                caches.add(Path(*physical_root.parts[: index + 1]).resolve())
            if parts[index : index + 2] == ["hosted", "pub.dev"]:
                caches.add(Path(*physical_root.parts[: index + 2]).resolve())
            if parts[index : index + 2] == ["pub", "cache"]:
                caches.add(Path(*physical_root.parts[: index + 2]).resolve())
    return caches


def _inside(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
        return True
    except ValueError:
        return False


def _is_app_root(config_path: Path, package_root: Path) -> bool:
    """Only the package_config entry pointing at its owning project is the app root."""
    if config_path.parent.name != ".dart_tool":
        return False
    return package_root.resolve() == config_path.parent.parent.resolve()


def _load_config(path: Path) -> list[dict[str, str]]:
    if _is_symlink_path(path):
        raise CollectionError("package_config.json must not be a symlink")
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise CollectionError(f"cannot read package_config.json: {error}") from error
    entries = document.get("packages") if isinstance(document, dict) else None
    if not isinstance(entries, list):
        raise CollectionError("package_config.json must contain a packages array")
    result: list[dict[str, str]] = []
    names: set[str] = set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict) or not isinstance(entry.get("name"), str):
            raise CollectionError(f"invalid package_config entry at index {index}")
        name = entry["name"]
        if not _PACKAGE_NAME.fullmatch(name):
            raise CollectionError(f"unsafe package name in package_config: {name!r}")
        if name in names:
            raise CollectionError(f"duplicate package_config entry: {name!r}")
        root_uri = entry.get("rootUri")
        if not isinstance(root_uri, str) or not root_uri:
            raise CollectionError(f"missing rootUri for package {name!r}")
        names.add(name)
        result.append({"name": name, "rootUri": root_uri})
    return result


def collect(package_config: Path, lockfile: Path, output: Path) -> dict[str, object]:
    config_path = package_config.absolute()
    lock_path = lockfile.absolute()
    if _is_symlink_path(lock_path):
        raise CollectionError("pubspec.lock must not be a symlink")
    if not config_path.is_file() or not lock_path.is_file():
        raise CollectionError("package_config.json and pubspec.lock must be regular files")
    entries = _load_config(config_path)
    locked = parse_lockfile(lock_path)

    resolved: list[tuple[dict[str, str], Path, dict[str, str | None] | None]] = []
    for entry in entries:
        root = _package_root(config_path, entry["rootUri"])
        lock_entry = locked.get(entry["name"])
        if lock_entry is None and not _is_app_root(config_path, root):
            raise CollectionError(
                f"package_config entry {entry['name']!r} is not the application root and has no parsed lockfile entry"
            )
        resolved.append((entry, root, lock_entry))

    package_roots = [root for entry, root, lock_entry in resolved if lock_entry and lock_entry["source"] != "sdk"]
    caches = _cache_roots(package_roots)
    if output.exists() or output.is_symlink():
        raise CollectionError("output must be a new directory that does not already exist")
    parent = output.parent.absolute()
    if not parent.is_dir():
        raise CollectionError("output parent directory must already exist")
    output_path = parent.resolve() / output.name
    if output_path.name in {".", ".."}:
        raise CollectionError("invalid output directory")
    if any(_inside(output_path, cache) for cache in caches):
        raise CollectionError("output may not be inside an inferred package cache")
    if any(_inside(output_path, root.resolve()) for _, root, _ in resolved if root.exists()):
        raise CollectionError("output may not be inside a resolved package directory")

    manifest_packages: list[dict[str, object]] = []
    missing: list[dict[str, str]] = []
    planned_files: list[tuple[Path, Path, dict[str, object]]] = []
    used_package_dirs: set[str] = set()

    for entry, root, lock_entry in resolved:
        name = entry["name"]
        source = lock_entry["source"] if lock_entry else None
        version = lock_entry["version"] if lock_entry else None
        if lock_entry and (not source or not version):
            raise CollectionError(f"lockfile entry for {name!r} lacks source or version")
        is_root = lock_entry is None and _is_app_root(config_path, root)
        resolution = "sdk" if source == "sdk" else "locked" if lock_entry else "root"
        public_url = _safe_external_url(lock_entry["url"] if lock_entry else None)
        package_record: dict[str, object] = {
            "name": name,
            "version": version,
            "resolution": resolution,
            "source": source or "root",
            "license_files": [],
        }
        if public_url:
            package_record["upstream_url"] = public_url if source != "hosted" else None
            if source == "hosted":
                package_record["registry_url"] = public_url
                package_record.pop("upstream_url")
        if _commercial_review(name, version):
            package_record["review_flags"] = ["commercial-license-review"]
        manifest_packages.append(package_record)

        if is_root:
            package_record["notice_status"] = "application-root-not-collected"
            continue
        if _is_symlink_path(root):
            missing.append({"name": name, "version": version or "", "reason": "package-root-symlink-not-followed"})
            package_record["notice_status"] = "package-root-symlink-not-followed"
            continue
        if not root.is_dir():
            missing.append({"name": name, "version": version or "", "reason": "package-root-missing"})
            package_record["notice_status"] = "package-root-missing"
            continue

        package_dir_name = _safe_package_component(name)
        if version:
            package_dir_name += "-" + _safe_package_component(version)
        if package_dir_name in used_package_dirs:
            raise CollectionError(f"package output directory collision: {package_dir_name!r}")
        used_package_dirs.add(package_dir_name)
        package_files: list[dict[str, str]] = []
        skipped_symlink = False
        try:
            children = sorted(root.iterdir(), key=lambda candidate: candidate.name.casefold())
        except OSError as error:
            raise CollectionError(f"cannot list package root for {name!r}: {error}") from error
        for source_file in children:
            lower_name = source_file.name.casefold()
            if not lower_name.startswith(_NOTICE_PREFIXES):
                continue
            if _is_symlink_path(source_file):
                skipped_symlink = True
                continue
            try:
                file_stat = source_file.stat(follow_symlinks=False)
            except OSError:
                skipped_symlink = True
                continue
            if not stat.S_ISREG(file_stat.st_mode):
                continue
            if not re.fullmatch(r"[A-Za-z0-9._-]+", source_file.name):
                raise CollectionError(f"unsafe notice filename in {name!r}: {source_file.name!r}")
            data = source_file.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            relative = Path("packages") / package_dir_name / _safe_filename(source_file.name)
            if any(item[1] == relative for item in planned_files):
                raise CollectionError(f"notice output filename collision: {relative.as_posix()}")
            file_record = {"file": relative.as_posix(), "sha256": digest}
            package_files.append(file_record)
            planned_files.append((source_file, relative, file_record))
        package_record["license_files"] = package_files
        if not package_files:
            reason = "matching-notice-files-are-symlinks" if skipped_symlink else "no-root-license-or-notice-files"
            missing.append({"name": name, "version": version or "", "reason": reason})
            package_record["notice_status"] = reason
        else:
            package_record["notice_status"] = "collected"

    flagged_packages = [
        {"name": item["name"], "version": item["version"]}
        for item in manifest_packages
        if item.get("review_flags")
    ]
    review_flags = []
    if flagged_packages:
        review_flags.append(
            {
                "flag": "commercial-license-review",
                "packages": flagged_packages,
                "basis": "locked FlutterBluePlus package-family major version; review exact package license terms",
                "clearance": False,
            }
        )
    manifest: dict[str, object] = {
        "format_version": 1,
        "purpose": "license-notice inventory only; not legal clearance",
        "packages": manifest_packages,
        "missing_notices": missing,
        "review_flags": review_flags,
    }

    staging = Path(tempfile.mkdtemp(prefix=f".{output.name}.notices-", dir=parent.resolve()))
    try:
        for source_file, relative, file_record in planned_files:
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source_file, target, follow_symlinks=False)
            if hashlib.sha256(target.read_bytes()).hexdigest() != file_record["sha256"]:
                raise CollectionError(f"copied notice hash mismatch: {relative.as_posix()}")
        (staging / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        (staging / "missing-notices.json").write_text(
            json.dumps(missing, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        os.rename(staging, output_path)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package_config", type=Path, help="path to .dart_tool/package_config.json")
    parser.add_argument("pubspec_lock", type=Path, help="path to pubspec.lock")
    parser.add_argument("--output", required=True, type=Path, help="new output directory")
    args = parser.parse_args(argv)
    try:
        manifest = collect(args.package_config, args.pubspec_lock, args.output)
    except CollectionError as error:
        parser.error(str(error))
    counts = len(manifest["packages"])
    missing = len(manifest["missing_notices"])
    print(f"Collected notices for {counts} package entries; {missing} package entries need notice review.")
    print("Inventory only; this output is not license compatibility or distribution clearance.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
