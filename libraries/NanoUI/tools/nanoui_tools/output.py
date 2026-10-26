"""Staged resource replacement with last-good backups and exception rollback."""

import os
from pathlib import Path
import tempfile


def replace_outputs(outputs):
    paths = [Path(path).resolve() for path in outputs]
    if len(set(paths)) != len(paths):
        raise ValueError("Output paths collide")
    previous = {}
    staged = {}
    replaced = []
    try:
        for path, text in zip(paths, outputs.values()):
            path.parent.mkdir(parents=True, exist_ok=True)
            previous[path] = path.read_bytes() if path.exists() else None
            with tempfile.NamedTemporaryFile(
                dir=path.parent, prefix=".nanoui-", delete=False
            ) as file:
                staged[path] = Path(file.name)
                file.write(text.encode("utf-8"))
        # A .bak is the previous generated version, not an accumulating history.
        for path, old in previous.items():
            if old is not None:
                backup = path.with_suffix(path.suffix + ".bak")
                with tempfile.NamedTemporaryFile(
                    dir=path.parent, prefix=".nanoui-", delete=False
                ) as file:
                    backup_temp = Path(file.name)
                    file.write(old)
                try:
                    os.replace(backup_temp, backup)
                finally:
                    backup_temp.unlink(missing_ok=True)
        for path in paths:
            os.replace(staged[path], path)
            replaced.append(path)
    except Exception:
        for path in reversed(replaced):
            old = previous[path]
            if old is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(old)
        raise
    finally:
        for temporary in staged.values():
            temporary.unlink(missing_ok=True)


def existing_parts(config):
    header = config.output_h.read_text(encoding="utf-8") if config.output_h.exists() else ""
    cpp = config.output_cpp.read_text(encoding="utf-8") if config.output_cpp.exists() else ""
    return header, cpp
