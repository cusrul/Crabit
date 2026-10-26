#!/usr/bin/env python3
"""Validate the shared NanoUI asset manifest without generating firmware files."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

RANGE_RE = re.compile(r"^U\+([0-9A-Fa-f]{4,6})(?:-U\+([0-9A-Fa-f]{4,6}))?$")


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def codepoints(manifest: dict, errors: list[str]) -> set[int]:
    values: set[int] = set()
    for entry in manifest.get("fonts", []):
        for raw in entry.get("ranges", []):
            match = RANGE_RE.match(raw)
            if not match:
                fail(errors, f"invalid Unicode range: {raw}")
                continue
            start = int(match.group(1), 16)
            end = int(match.group(2), 16) if match.group(2) else start
            if end < start or end > 0x10FFFF:
                fail(errors, f"invalid Unicode range bounds: {raw}")
                continue
            values.update(range(start, end + 1))
    for text in manifest.get("required_strings", []):
        if not isinstance(text, str):
            fail(errors, "required_strings must contain strings")
            continue
        values.update(ord(char) for char in text)
    return values


def sha256_files(root: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    total = 0
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        rel = path.relative_to(root).as_posix().encode("utf-8")
        data = path.read_bytes()
        digest.update(rel + b"\0" + data)
        total += len(data)
    return total, digest.hexdigest()


def validate(repo: Path, manifest: dict) -> dict:
    errors: list[str] = []
    profiles = manifest.get("profiles", {})
    if manifest.get("schema") != 1:
        fail(errors, "schema must be 1")
    if not profiles:
        fail(errors, "profiles must not be empty")

    ids: set[str] = set()
    for entry in manifest.get("fonts", []):
        identifier = entry.get("id")
        if not isinstance(identifier, str) or not identifier:
            fail(errors, "every font must have a non-empty id")
        elif identifier in ids:
            fail(errors, f"duplicate font id: {identifier}")
        else:
            ids.add(identifier)
        for profile in entry.get("profiles", []):
            if profile not in profiles:
                fail(errors, f"font {identifier} references unknown profile: {profile}")

    report_profiles: dict[str, dict] = {}
    for name, profile in profiles.items():
        roots = [
            profile.get("asset_root"),
            profile.get("font_source"),
            *profile.get("source_roots", []),
        ]
        for raw in roots:
            if not raw:
                fail(errors, f"profile {name} contains an empty path")
                continue
            path = repo / raw
            if not path.exists():
                fail(errors, f"profile {name} path does not exist: {raw}")
        root = repo / profile.get("asset_root", ".")
        total, digest = sha256_files(root) if root.is_dir() else (0, "")
        budget = profile.get("budget_bytes")
        if not isinstance(budget, int) or budget <= 0:
            fail(errors, f"profile {name} must have a positive budget_bytes")
        report_profiles[name] = {
            "source_bytes": total,
            "source_sha256": digest,
            "generated_budget_bytes": budget,
        }

    cps = codepoints(manifest, errors)
    if any(point > 0xFFFF for point in cps) and manifest.get("schema") != 1:
        fail(errors, "non-BMP codepoints require the uint32 schema")
    return {
        "schema": manifest.get("schema"),
        "profiles": report_profiles,
        "required_codepoints": len(cps),
        "errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--manifest", type=Path, default=Path("firmware/nanoui/assets/manifest.json")
    )
    parser.add_argument(
        "--json", action="store_true", help="print the deterministic report as JSON"
    )
    args = parser.parse_args()
    repo = args.repo.resolve()
    manifest_path = args.manifest if args.manifest.is_absolute() else repo / args.manifest
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"asset manifest error: {exc}", file=sys.stderr)
        return 2
    report = validate(repo, manifest)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
    else:
        for name, profile in report["profiles"].items():
            print(
                f"{name}: source {profile['source_bytes']} bytes / generated budget {profile['generated_budget_bytes']} / {profile['source_sha256']}"
            )
        print(f"required Unicode codepoints: {report['required_codepoints']}")
        for error in report["errors"]:
            print(f"error: {error}", file=sys.stderr)
    return 1 if report["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
