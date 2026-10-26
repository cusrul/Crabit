#!/usr/bin/env python3
"""Run pinned formatters only on explicitly registered repository targets."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Callable, Sequence


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "tools" / "style_scope.json"
VERSIONS = {"cpp": "19.1.1", "python": "24.4.2", "dart": "3.11.4"}
EXTENSIONS = {"cpp": {".h", ".hpp", ".c", ".cpp", ".ino"}, "python": {".py"}, "dart": {".dart"}}
ENV_TOOLS = {"cpp": "CRABIT_CLANG_FORMAT", "python": "CRABIT_BLACK", "dart": "CRABIT_DART"}
GENERATED = (".g.dart", ".freezed.dart")
SKIP_DIRS = {"build", "vendor", "node_modules", "reference", "drivers", "middlewares"}


class StyleError(ValueError):
    """Invalid style manifest, scope, or formatter setup."""


def _inside(root: Path, path: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _valid_source(path: Path, language: str) -> bool:
    name = path.name.lower()
    return (
        path.suffix.lower() in EXTENSIONS[language]
        and not name.endswith(GENERATED)
        and ".local." not in name
        and not name.endswith(("_assets.h", "_assets.cpp", "_font_provider.cpp"))
        and not any(
            part.lower() in SKIP_DIRS | {"resource", "resources", "assets"} for part in path.parts
        )
    )


def load_targets(root: Path = ROOT, manifest: Path = MANIFEST) -> dict[str, list[Path]]:
    root = root.resolve()
    manifest = manifest.resolve()
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise StyleError(f"cannot read style manifest {manifest}: {exc}") from exc
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        raise StyleError("style manifest schema_version must be 1")
    versions, targets = data.get("versions"), data.get("targets")
    if versions != VERSIONS:
        raise StyleError(f"style manifest versions must be exactly {VERSIONS}")
    if not isinstance(targets, dict) or set(targets) != set(VERSIONS):
        raise StyleError("targets must define cpp, python, and dart arrays")
    found: dict[str, list[Path]] = {}
    claimed: set[Path] = set()
    for language in VERSIONS:
        entries = targets[language]
        if not isinstance(entries, list) or not entries:
            raise StyleError(f"targets.{language} must be a non-empty array")
        selected: set[Path] = set()
        for value in entries:
            if not isinstance(value, str) or not value.strip():
                raise StyleError(f"targets.{language} entries must be relative paths")
            raw = Path(value)
            if raw.is_absolute() or ".." in raw.parts or "\\" in value or ":" in value:
                raise StyleError(f"target must be repository-relative: {value}")
            path = (root / raw).resolve()
            if not _inside(root, path) or not path.exists():
                raise StyleError(f"target missing or outside repository: {value}")
            if path.is_file():
                if not _valid_source(path.relative_to(root), language):
                    raise StyleError(f"unsupported, generated, or resource file: {value}")
                files = [path]
            elif path.is_dir():
                files = [
                    item
                    for item in path.rglob("*")
                    if item.is_file()
                    and not any(
                        part.lower() in SKIP_DIRS for part in item.relative_to(path).parts[:-1]
                    )
                    and _valid_source(item.relative_to(root), language)
                ]
            else:
                raise StyleError(f"target is not a file or directory: {value}")
            for file in files:
                resolved = file.resolve()
                if not _inside(root, resolved) or not _valid_source(
                    resolved.relative_to(root), language
                ):
                    raise StyleError(f"target resolves outside allowed source scope: {file}")
                if resolved in selected or resolved in claimed:
                    raise StyleError(f"duplicate style target: {file.relative_to(root)}")
                selected.add(resolved)
                claimed.add(resolved)
        if not selected:
            raise StyleError(f"targets.{language} selects no source files")
        found[language] = sorted(selected)
    return found


def resolve_tool(language: str, root: Path = ROOT) -> str:
    env = os.environ.get(ENV_TOOLS[language])
    if env:
        candidate = str(Path(env).expanduser())
    else:
        name = {"cpp": "clang-format", "python": "black", "dart": "dart"}[language]
        candidate = shutil.which(name) or ""
        if not candidate and language == "cpp" and os.name == "nt":
            program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
            for edition in ("Community", "Professional", "Enterprise", "BuildTools"):
                fallback = (
                    program_files
                    / "Microsoft Visual Studio"
                    / "2022"
                    / edition
                    / "VC"
                    / "Tools"
                    / "Llvm"
                    / "bin"
                    / "clang-format.exe"
                )
                if fallback.is_file():
                    candidate = str(fallback)
                    break
    if not candidate or (os.path.sep in candidate and not Path(candidate).is_file()):
        raise StyleError(
            f"missing formatter for {language}; configure {ENV_TOOLS[language]} or PATH"
        )
    return candidate


def _argv(tool: str, args: list[str]) -> list[str]:
    if os.name == "nt" and tool.lower().endswith((".bat", ".cmd")):
        # cmd interprets metacharacters even when passed through list2cmdline.
        # Refuse ambiguous batch paths rather than execute shell syntax.
        if any(re.search(r'[&|<>^%!\r\n"]', value) for value in [tool, *args]):
            raise StyleError("unsafe character in Windows batch formatter argument")
        return [
            os.environ.get("COMSPEC", "cmd.exe"),
            "/d",
            "/s",
            "/c",
            subprocess.list2cmdline([tool, *args]),
        ]
    return [tool, *args]


def run(
    language: str,
    action: str,
    files: list[Path],
    root: Path = ROOT,
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
) -> None:
    if language == "cpp" and not (root / ".clang-format").is_file():
        raise StyleError("repository .clang-format is required")
    tool = resolve_tool(language, root)
    version = runner(
        _argv(tool, ["--version"]), cwd=root, capture_output=True, text=True, check=False
    )
    output = (version.stdout or "") + (version.stderr or "")
    expected = VERSIONS[language]
    if version.returncode or not re.search(rf"(?<![\w.]){re.escape(expected)}(?![\w.+-])", output):
        raise StyleError(
            f"{language} formatter must report version {expected}; got {output.strip()!r}"
        )
    paths = [str(path) for path in files]
    if language == "cpp":
        args = [f"--style=file:{root / '.clang-format'}", "--fallback-style=none"]
        args += ["--dry-run", "--Werror"] if action == "check" else ["-i"]
    elif language == "python":
        args = ["--config", str(root / "pyproject.toml")]
        args += ["--check"] if action == "check" else []
    else:
        args = (
            ["format", "--output=none", "--set-exit-if-changed"]
            if action == "check"
            else ["format", "--output=write"]
        )
    # Windows batch commands have a much smaller limit than CreateProcess.
    # Keep the registered scope explicit without passing one huge argument list.
    for start in range(0, len(paths), 16):
        result = runner(_argv(tool, [*args, *paths[start : start + 16]]), cwd=root, check=False)
        if result.returncode:
            raise StyleError(
                f"{language} formatter {action} failed with exit code {result.returncode}"
            )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("list", "check", "fix"))
    parser.add_argument("--language", choices=("cpp", "python", "dart", "all"), default="all")
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    args = parser.parse_args(argv)
    try:
        targets = load_targets(ROOT, args.manifest)
        languages = list(VERSIONS) if args.language == "all" else [args.language]
        for language in languages:
            files = targets[language]
            if args.action == "list":
                print(f"{language}:\n" + "\n".join(str(path.relative_to(ROOT)) for path in files))
            else:
                run(language, args.action, files)
                print(f"{language}: {args.action} passed ({len(files)} files)")
        return 0
    except (StyleError, OSError) as exc:
        print(f"style: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
