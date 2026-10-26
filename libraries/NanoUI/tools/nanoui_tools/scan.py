"""Lightweight source scanner; explicit runtime text remains supported."""

import ast
from pathlib import Path
import re

COMMENT_OR_STRING = re.compile(r'"(?:[^"\\]|\\.)*"|//[^\n]*|/\*.*?\*/', re.S)
STRINGS = re.compile(r'"((?:[^"\\]|\\.)*)"')
ART = re.compile(r'STYLE_ART\s*\(\s*"((?:[^"\\]|\\.)*)"')
VISIBLE = [
    re.compile(r'new\s+(?:Text|SlidingText|MarqueeText)\s*\(\s*"((?:[^"\\]|\\.)*)"'),
    re.compile(r'drawText(?:InRect)?\s*\([^;]*"((?:[^"\\]|\\.)*)"'),
    re.compile(r'\{\s*(?:nullptr|[A-Za-z_]\w*)\s*,\s*"((?:[^"\\]|\\.)*)"'),
]
SIZE_PATTERNS = [
    re.compile(r"new\s+(?:Text|SlidingText|MarqueeText)\s*\([^,;()]+,\s*(\d+)"),
    re.compile(r"drawTextInRect\s*\([^,;()]+,[^,;()]+,[^,;()]+,[^,;()]+,[^,;()]+,\s*(\d+)\)"),
    re.compile(r"drawText\s*\([^,;()]+,[^,;()]+,[^,;()]+,\s*(\d+)"),
]


def unescape(text):
    try:
        return ast.literal_eval('"' + text + '"')
    except (ValueError, SyntaxError):
        return text


def scan_project(dirs, mode="visible", extra_text=(), extra_sizes=(), excluded=(), helpers=()):
    chars = set("0123456789:./- %[]<>()+=")
    art_chars = set()
    sizes = {12, 16, *extra_sizes}
    excluded = {Path(path).resolve() for path in excluded}
    visited = set()
    patterns = VISIBLE + [
        re.compile(r"\b" + re.escape(name) + r'\s*\(\s*"((?:[^"\\]|\\.)*)"') for name in helpers
    ]
    for directory in dirs:
        for path in sorted(Path(directory).rglob("*")):
            if path.suffix.lower() not in (".h", ".hpp", ".cpp", ".c", ".ino"):
                continue
            real = path.resolve()
            if real in visited or real in excluded:
                continue
            if any(
                part in ("experimental", "generated", "build", ".git", "__pycache__")
                or "Backup" in part
                for part in path.relative_to(directory).parts
            ):
                continue
            visited.add(real)
            try:
                raw = path.read_text(encoding="utf-8-sig")
            except UnicodeDecodeError:
                raw = path.read_text(encoding="gbk")
            raw = COMMENT_OR_STRING.sub(lambda m: m[0] if m[0].startswith('"') else "", raw)
            for pattern in SIZE_PATTERNS:
                sizes.update(int(value) for value in pattern.findall(raw) if 8 <= int(value) <= 64)
            for pattern in [STRINGS] if mode == "all" else patterns:
                for value in pattern.findall(raw):
                    chars.update(c for c in unescape(value) if ord(c) >= 32)
            for value in ART.findall(raw):
                art_chars.update(c for c in unescape(value) if ord(c) >= 32)
    chars.update(art_chars)
    for text in extra_text:
        chars.update(c for c in text if ord(c) >= 32)
    return sorted(chars), sorted(art_chars), sorted(sizes)
