"""Explicit skips for guards pinned to revisions that only exist in a maintainer checkout.

Several source-immutability tests compare today's files with revisions from the
project's private development history. A clean clone of this repository cannot
resolve those objects, so the guards are skipped with an explicit reason instead
of failing with a Git error. Where the revisions do exist the same guards run
unchanged; nothing is silently downgraded to a pass.
"""

from __future__ import annotations

import functools
import subprocess
import unittest
from pathlib import Path


def revision_available(repo: Path, revision: str) -> bool:
    result = subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", f"{revision}^{{commit}}"],
        cwd=repo,
        capture_output=True,
        text=True,
    )
    return result.returncode == 0 and bool(result.stdout.strip())


def require_history(repo: Path, *revisions: str) -> None:
    """Skip the calling test when any pinned revision is unavailable here."""
    missing = [revision for revision in revisions if not revision_available(repo, revision)]
    if missing:
        raise unittest.SkipTest(
            "pinned development revision(s) unavailable in this clone: " + ", ".join(missing)
        )


def history_guard(repo: Path, *revisions: str):
    """Decorator form of :func:`require_history` for single test methods."""

    def decorate(function):
        @functools.wraps(function)
        def wrapper(*args, **kwargs):
            require_history(repo, *revisions)
            return function(*args, **kwargs)

        return wrapper

    return decorate
