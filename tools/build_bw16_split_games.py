"""Experimental paired BW16 host build. No upload; hardware is NOT TESTED.

Run serially (never alongside another Arduino/SDK build). --output must not exist.
Extra --game-namespace values are C++ namespace/class identifiers, not wildcards.
The C++ registry holds all 27 games in five gameplay categories; --arduboy30
selects catalogue ports on top of the six built-in games (default: none).
Never flatten the ELF across the protected settings/FTL gap or upload main alone.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from collections.abc import Iterator
from typing import NamedTuple

REPO = Path(__file__).resolve().parents[1]
LOCAL_APP_DATA = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
DEFAULT_SDK = LOCAL_APP_DATA / "Arduino15/packages/realtek/hardware/AmebaD/3.1.9"
EXT_OFFSET, EXT_END, XIP_BASE = 0x107000, 0x1F8000, 0x08000000
# Same manifest the PowerShell wrapper reads; it is the only list of staged headers.
SHARED_MANIFEST = Path("tools/bw16_shared_headers.json")
SHARED_MANIFEST_KEYS = frozenset({"$comment", "source_root", "staged_root", "headers"})
SHARED_SOURCE_ROOT, SHARED_STAGED_ROOT = "firmware/shared", "src/crabit_shared"
SHARED_HEADER_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\.h")
GAME_NAMESPACES = ("Arduboy3DSandbox", "CastleBoySandbox", "HollowSeekerSandbox", "SameGameSandbox", "LasersSandbox",
                   "HopperSandbox")
# Hopper's engine lives in HopperSandbox inside the shared port TU. AppHopper is
# still a selector below; HopperSandbox is the namespace that actually owns the
# game's code and PROGMEM assets, so both are selected exactly (no broad match).
ARDUBOY30_CATALOG = Path("docs/nanoui/arduboy30-catalog.json")
ARDUBOY30_REGISTRY = Path("firmware/rtl8720dn/NanoUI/app/arduboy30_registry.h")
ARDUBOY30_SCHEMA = "crabit-arduboy30-catalog-v1"
ARDUBOY30_PORTED = frozenset({"ported-host-verified", "ported-embedded-verified"})
ARDUBOY30_ID = re.compile(r"[a-z0-9]+(?:[-_][a-z0-9]+)*")
ARDUBOY30_CPP = re.compile(r"[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)*")
# One C++ registry row: entryFor<T>("id", "label", Category::X) for a
# selectable catalogue port, builtInEntryFor<T>(...) for a game that is part of
# every ports image. Both use the same factory; only selection differs.
ARDUBOY30_ENTRY = re.compile(
    r'(?P<kind>builtInEntryFor|entryFor)<(?P<app_class>[A-Za-z_][A-Za-z_0-9]*)>'
    r'\(\s*"(?P<app_id>[a-z0-9]+(?:[-_][a-z0-9]+)*)"\s*,\s*'
    r'"(?P<label>(?:[^"\\]|\\.)*)"\s*,\s*Category::(?P<category>[A-Za-z_][A-Za-z_0-9]*)\s*\)')
# The six games that predate the catalogue: always present, never selectable.
# Must equal the catalogue's existing_games_excluded.
ARDUBOY30_BUILT_IN = frozenset({
    "arduboy3d", "castle_boy", "hopper", "hollow", "lasers", "samegame"})
ARDUBOY30_CATEGORIES = ("Shooter", "JumpAction", "Puzzle", "Board", "CasualRhythm")
ARDUBOY30_CATEGORY_SET = frozenset(ARDUBOY30_CATEGORIES)
# Bounded link experiment: the category menu exists in every ports image, so
# its class is placed in .extflash even when no catalogue port is selected.
ARDUBOY30_FOLDER_SELECTOR = "ArduboyCategoryFolderApp"
# Read-only Arduboy game support that the class-name selectors above miss.
# All of it is emitted from Arduboy2Compat.h / ArduboyPortRuntime.h into the
# single main_entry translation unit. The nine support symbols below were the
# complete residue in retry2 (1,484 bytes); the registry table row grew with
# the unified 27-entry menu, so its size is no longer a fixed 264 bytes:
#   SpritesCompat::draw / drawPlusMask        532 B
#   CrabitArduboy::detail runtime+allocator   208 B
#   arduboyFont5x7()::font                    480 B
#   Arduboy30Registry::entries(...)::table    grew with the unified registry
#   buildArduboyCategoryMenu()::rows           new five-row category table
# Mutable port state stays in .bss/.data (only .text* and .rodata* match). The
# Mono audio is also shared by the built-in six and catalogue ports. Its narrow
# namespace selector covers code/read-only score helpers, never mutable state.
# Category menu, audio and registry are compiled in every Arduboy-ports image, so
# these placements always apply; they are verified as "if emitted, it must be
# in .extflash" instead of being required in every selection.
ARDUBOY30_SUPPORT_SELECTORS = ("SpritesCompat", "CrabitArduboy::detail",
                               "arduboyFont5x7", "Arduboy30Registry",
                               "buildArduboyCategoryMenu", "CrabitArduboyAudio")
# Section names whose objects are mutable RAM state, not read-only game content.
# `objdump -t` reports the port runtime's lazy singleton as weak objects in
# .ram_image2.bss (guard variable + instance at 0x10024318/0x1002431c); they are
# weak only because the header defines them inline, and moving them to flash
# would be wrong. The optional-support guard therefore allows STT_OBJECT
# symbols in these sections and nothing else outside .extflash.
MUTABLE_RAM_SECTIONS = frozenset({
    ".ram_image2.data", ".ram_image2.bss", ".ram_image2.nocache.data", ".ram_heap.data",
    ".psram_image2.data", ".psram_image2.bss", ".psram_heap.data",
})
# Each libm archive member below contains exactly one global code symbol
# (atanf also owns its 0x20-byte .rodata constants; sqrt owns a 5-byte string).
# Members are placed whole so no broad *sqrt*/std/NanoUI pattern is needed.
# The first five entries are the original float/sqrt whitelist and stay
# byte-for-byte unchanged. The remaining 24 are the complete libm residue in
# the audited 27-port image (build/arduboy27-audio-final-20261116/
# audit-main2ext-20261117/q_libm.txt): .text plus .rodata only, 9796 B total.
# Every member has an empty .data/.bss; their read-only tables (npio2_hw,
# two_over_pi, init_jk, PIo2, atanhi/atanlo) ride along in .rodata*. Nothing
# here force-loads the whole library; an absent member is not an error.
ARDUBOY30_MATH_OBJECTS = (
    ("lib_a-sf_atan.o", "atanf", (".text*", ".rodata*")),
    ("lib_a-ef_atan2.o", "__ieee754_atan2f", (".text*",)),
    ("lib_a-wf_atan2.o", "atan2f", (".text*",)),
    ("lib_a-e_sqrt.o", "__ieee754_sqrt", (".text*",)),
    ("lib_a-w_sqrt.o", "sqrt", (".text*", ".rodata*")),
    ("lib_a-k_rem_pio2.o", "__kernel_rem_pio2", (".text*", ".rodata*")),
    ("lib_a-ef_rem_pio2.o", "__ieee754_rem_pio2f", (".text*", ".rodata*")),
    ("lib_a-e_rem_pio2.o", "__ieee754_rem_pio2", (".text*", ".rodata*")),
    ("lib_a-kf_rem_pio2.o", "__kernel_rem_pio2f", (".text*", ".rodata*")),
    ("lib_a-s_atan.o", "atan", (".text*", ".rodata*")),
    ("lib_a-k_cos.o", "__kernel_cos", (".text*", ".rodata*")),
    ("lib_a-e_atan2.o", "__ieee754_atan2", (".text*", ".rodata*")),
    ("lib_a-k_sin.o", "__kernel_sin", (".text*", ".rodata*")),
    ("lib_a-s_floor.o", "floor", (".text*", ".rodata*")),
    ("lib_a-s_scalbn.o", "scalbn", (".text*", ".rodata*")),
    ("lib_a-sf_scalbn.o", "scalbnf", (".text*", ".rodata*")),
    ("lib_a-kf_cos.o", "__kernel_cosf", (".text*", ".rodata*")),
    ("lib_a-s_sin.o", "sin", (".text*", ".rodata*")),
    ("lib_a-kf_sin.o", "__kernel_sinf", (".text*", ".rodata*")),
    ("lib_a-s_cos.o", "cos", (".text*", ".rodata*")),
    ("lib_a-sf_sin.o", "sinf", (".text*", ".rodata*")),
    ("lib_a-sf_cos.o", "cosf", (".text*", ".rodata*")),
    ("lib_a-s_copysign.o", "copysign", (".text*", ".rodata*")),
    ("lib_a-sf_copysign.o", "copysignf", (".text*", ".rodata*")),
    ("lib_a-s_fabs.o", "fabs", (".text*", ".rodata*")),
    ("lib_a-sf_fabs.o", "fabsf", (".text*", ".rodata*")),
    ("lib_a-sf_floor.o", "floorf", (".text*", ".rodata*")),
    ("lib_a-w_atan2.o", "atan2", (".text*", ".rodata*")),
    ("lib_a-s_matherr.o", "matherr", (".text*", ".rodata*")),
)
# Read-only product resources that every split profile compiles (default six
# included). They are matched by exact input-section name, never by a class
# substring: a `font5x7` token would also capture arduboyFont5x7
# (`_ZZL14arduboyFont5x7vE4font`). Writable .data/.bss is never listed, and an
# unemitted resource in a reduced image is tolerated, not required.
RESOURCE_SECTIONS = (
    ("cn_bitmap_pool", ".rodata.cn_bitmap_pool"),
    ("cn_glyph_index", ".rodata.cn_glyph_index"),
    ("cn_glyph_count", ".rodata.cn_glyph_count"),
    ("asset_my_media", ".rodata.asset_my_media"),
    ("font5x7", ".rodata._ZL7font5x7"),
)
REQUIRED_GAME_LOGIC = ("HollowSeekerSandbox::updateGame", "SameGameSandbox::Game::updateGame",
                       "LasersSandbox::updateGame")
DEFAULT_SELECTORS = (*GAME_NAMESPACES, "Arduboy3DApp", "CastleBoyApp", "AppHopper",
                     "HollowSeekerApp", "SameGameApp", "LasersApp", "Arduboy2Compat",
                     "ArduboyCompatApp", "GamesFolderApp")
FAILURE = re.compile(
    r"fatal error:|\berror:|\berror [A-Z]+\d+|undefined reference|"
    r"cannot find|can't find|cannot open|could not|unable to|not recognized|no such file|file not found|"
    r"invalid (?:number of )?parameters?|system cannot|access is denied|"
    r"\berror\s+[1-9]\d*\b|\b(?:failed|failure)\b|找不到|参数无效|参数数量无效|拒绝访问", re.I)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def license_sha256(data: bytes) -> str:
    """Catalogue licence hashes are LF-normalized; CRLF checkouts still match."""
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def manifest_root(raw: object, expected: str, key: str) -> str:
    # Exact string match, no normalization: '/firmware/shared/' and 'firmware\\shared'
    # are rejected exactly like the PowerShell entry point rejects them.
    if not isinstance(raw, str) or raw != expected:
        raise ValueError(
            "Shared-header manifest must stage firmware/shared into src/crabit_shared: "
            f"{key}={raw!r}"
        )
    return raw


def manifest_header(raw: object) -> str:
    if not isinstance(raw, str) or not SHARED_HEADER_NAME.fullmatch(raw):
        raise ValueError(f"Shared-header manifest header must be a plain file name: {raw!r}")
    # 'foo..bar.h' is not a traversal, but both entry points reject it uniformly.
    if ".." in raw or raw in {".", ".."} or "/" in raw or "\\" in raw or Path(raw).is_absolute():
        raise ValueError(f"Shared-header manifest header must be a plain file name: {raw!r}")
    return raw


def load_shared_manifest(manifest: Path, repo: Path) -> tuple[Path, str, tuple[str, ...]]:
    """Read the one staging manifest both BW16 entry points use; no second list."""
    if not manifest.is_file():
        raise ValueError(f"Shared-header manifest missing: {manifest}")
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Shared-header manifest unreadable: {manifest}") from error
    if not isinstance(data, dict):
        raise ValueError("Shared-header manifest must be a JSON object")
    unexpected = set(data) - SHARED_MANIFEST_KEYS
    if unexpected:
        raise ValueError(f"Shared-header manifest has unexpected keys: {sorted(unexpected)}")
    if not isinstance(data.get("$comment"), str):
        raise ValueError("Shared-header manifest $comment must document the source of truth")
    source = repo / manifest_root(data.get("source_root"), SHARED_SOURCE_ROOT, "source_root")
    staged = manifest_root(data.get("staged_root"), SHARED_STAGED_ROOT, "staged_root")
    listed = data.get("headers")
    if not isinstance(listed, list) or not listed:
        raise ValueError("Shared-header manifest must list headers as a non-empty array")
    names = tuple(manifest_header(item) for item in listed)
    # Case-insensitive duplicates are rejected, matching the wrapper's Sort-Object -Unique.
    if len({name.casefold() for name in names}) != len(names):
        raise ValueError("Shared-header manifest lists a header more than once")
    return source, staged, names


def shared_staging(manifest: Path, repo: Path, sketch: Path) -> tuple[tuple[Path, str], ...]:
    """Resolve manifest entries to (source of truth, staged-relative path); fail early."""
    source, staged, names = load_shared_manifest(manifest, repo)
    pairs = []
    for name in names:
        origin = source / name
        if not origin.is_file():
            raise ValueError(f"Shared-header manifest header missing from firmware/shared: {name}")
        if not (sketch / staged / name).is_file():
            raise ValueError(f"Shared-header manifest header has no sketch target: {name}")
        pairs.append((origin, f"{staged}/{name}"))
    return tuple(pairs)


def stage_shared_headers(sketch: Path, pairs: tuple[tuple[Path, str], ...]) -> None:
    for origin, relative in pairs:
        target = sketch / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin, target)
        if sha256(target) != sha256(origin):
            raise ValueError(f"Staged header differs from firmware/shared: {target}")


def load_arduboy30_catalog(repo: Path) -> dict:
    """Read the one versioned candidate/assignment catalogue; never guess ids."""
    path = repo / ARDUBOY30_CATALOG
    if not path.is_file():
        raise ValueError(f"Arduboy 30 catalogue missing: {path}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Arduboy 30 catalogue unreadable: {path}") from error
    if not isinstance(data, dict) or data.get("schema") != ARDUBOY30_SCHEMA:
        raise ValueError("Arduboy 30 catalogue schema mismatch")
    games = data.get("games")
    if not isinstance(games, list) or len(games) != 21:
        raise ValueError("Arduboy catalogue must list exactly 21 retained additional games")
    seen = set()
    for index, game in enumerate(games):
        if not isinstance(game, dict) or game.get("index") != index:
            raise ValueError("Arduboy 30 catalogue indices must be contiguous")
        if not isinstance(game.get("id"), str) or not ARDUBOY30_ID.fullmatch(game["id"]):
            raise ValueError(f"Arduboy 30 catalogue id is invalid: {game.get('id')!r}")
        if game["id"] in seen:
            raise ValueError(f"Arduboy 30 catalogue id is duplicated: {game['id']}")
        seen.add(game["id"])
        source = game.get("source")
        if not isinstance(source, dict):
            raise ValueError(f"Arduboy 30 source is missing: {game['id']}")
        for key in ("repository", "commit", "license", "license_path", "license_sha256"):
            if not isinstance(source.get(key), str) or not source[key]:
                raise ValueError(f"Arduboy 30 source lacks {key}: {game['id']}")
        if len(source["commit"]) < 12 or not re.fullmatch(r"[0-9a-f]{12,40}", source["commit"]):
            raise ValueError(f"Arduboy 30 source commit is not pinned: {game['id']}")
        if not re.fullmatch(r"[0-9a-f]{64}", source["license_sha256"]):
            raise ValueError(f"Arduboy 30 source license hash is not sha256: {game['id']}")
        if not isinstance(game.get("license_status"), str) or not game["license_status"]:
            raise ValueError(f"Arduboy 30 licence status is missing: {game['id']}")
    return data


def arduboy30_port_fields(game: dict) -> None:
    """Validate the launcher fields the same way for explicit ids and 'all'."""
    for key in ("namespace", "app_class", "menu_label"):
        value = game.get(key)
        if not isinstance(value, str) or not value:
            raise ValueError(f"Arduboy 30 port lacks {key}: {game['id']!r}")
        if key != "menu_label" and not ARDUBOY30_CPP.fullmatch(value):
            raise ValueError(f"Arduboy 30 {key} is not a C++ identifier: {game['id']!r}")


def arduboy30_selection(catalog: dict, requested: list[str]) -> list[dict]:
    """Select only catalogued, already verified ports; reject silent gaps."""
    if not requested:
        return []
    if "all" in requested:
        if len(requested) != 1:
            raise ValueError("--arduboy30 all cannot be combined with explicit ids")
        chosen = [game for game in catalog["games"]
                  if game.get("status") in ARDUBOY30_PORTED and
                  game.get("license_status") == "verified"]
        for game in chosen:
            arduboy30_port_fields(game)
        return chosen
    by_id = {game["id"]: game for game in catalog["games"]}
    chosen, seen = [], set()
    for name in requested:
        game = by_id.get(name)
        if game is None:
            raise ValueError(f"Unknown Arduboy 30 id: {name!r}")
        if game["id"] in seen:
            raise ValueError(f"Arduboy 30 id selected twice: {name!r}")
        if game.get("status") not in ARDUBOY30_PORTED:
            raise ValueError(f"Arduboy 30 port is not verified yet: {name!r} ({game.get('status')})")
        if game.get("license_status") != "verified":
            raise ValueError(f"Arduboy 30 licence is not verified: {name!r} "
                             f"({game.get('license_status')})")
        arduboy30_port_fields(game)
        seen.add(game["id"])
        chosen.append(game)
    return chosen


def arduboy30_header(selected: list[dict]) -> str:
    """Generate only the staged selection; classes/labels stay in C++."""
    nl = chr(10)
    lines = ["#ifndef CRABIT_ARDUBOY30_SELECTED_H", "#define CRABIT_ARDUBOY30_SELECTED_H", "",
             "// Generated by tools/build_bw16_split_games.py --arduboy30. Do not edit.",
             "// Labels, classes and factories come from app/arduboy30_registry.h.",
             f"#define CRABIT_ARDUBOY30_COUNT {len(selected)}"]
    macros = []
    for game in selected:
        macro = arduboy30_macro(game["id"])
        if macro in macros:
            raise ValueError(f"Arduboy 30 id collides after macro conversion: {game['id']!r}")
        macros.append(macro)
        lines.append(f"#define CRABIT_ARDUBOY30_ENABLE_{macro} 1")
    lines += ["", "#endif", ""]
    return nl.join(lines)


def arduboy30_macro(app_id: str) -> str:
    """Map an id to the CRABIT_ARDUBOY30_ENABLE_<ID> suffix; reject collisions."""
    return re.sub(r"[^A-Za-z0-9]", "_", app_id).upper()


class RegistryEntry(NamedTuple):
    label: str
    app_class: str
    category: str
    built_in: bool


def parse_arduboy30_registry(text: str) -> dict[str, RegistryEntry]:
    """Parse the single C++ menu table; the catalogue is not runtime metadata.

    A row is either a selectable catalogue port (`entryFor`, macro-gated) or a
    built-in game (`builtInEntryFor`, always compiled). The row kind must agree
    with the canonical id set, so neither kind can silently absorb the other.
    """
    entries: dict[str, RegistryEntry] = {}
    classes: set[str] = set()
    macros: set[str] = set()
    for match in ARDUBOY30_ENTRY.finditer(text):
        app_class = match["app_class"]
        app_id = match["app_id"]
        label = match["label"]
        category = match["category"]
        built_in = match["kind"] == "builtInEntryFor"
        if not label:
            raise ValueError(f"Arduboy 30 registry label is empty: {app_id}")
        if category not in ARDUBOY30_CATEGORY_SET:
            raise ValueError(f"Arduboy 30 registry category is unknown: {app_id} ({category})")
        if built_in != (app_id in ARDUBOY30_BUILT_IN):
            raise ValueError(
                f"Arduboy 30 registry row kind does not match its id: {app_id} "
                f"({'builtInEntryFor' if built_in else 'entryFor'})")
        if app_id in entries:
            raise ValueError(f"Arduboy 30 registry id is duplicated: {app_id}")
        if app_class in classes:
            raise ValueError(f"Arduboy 30 registry class is duplicated: {app_class}")
        macro = arduboy30_macro(app_id)
        if macro in macros:
            raise ValueError(f"Arduboy 30 registry macro collision: {app_id}")
        if f'#include "apps/games/{app_id}/app.h"' not in text:
            raise ValueError(f"Arduboy 30 registry lacks its app include: {app_id}")
        if not built_in and f"defined(CRABIT_ARDUBOY30_ENABLE_{macro})" not in text:
            raise ValueError(f"Arduboy 30 registry lacks its enable macro: {app_id}")
        entries[app_id] = RegistryEntry(label, app_class, category, built_in)
        classes.add(app_class)
        macros.add(macro)
    if not entries:
        raise ValueError("Arduboy 30 C++ registry has no entries")
    return entries


def load_arduboy30_registry_entries(repo: Path) -> tuple[str, dict[str, RegistryEntry]]:
    path = repo / ARDUBOY30_REGISTRY
    if not path.is_file():
        raise ValueError(f"Arduboy 30 C++ registry missing: {path}")
    text = path.read_text(encoding="utf-8")
    return text, parse_arduboy30_registry(text)


def load_arduboy30_registry(repo: Path) -> tuple[str, dict[str, tuple[str, str]]]:
    """Return the legacy (label, class) view used by per-port metadata checks."""
    text, entries = load_arduboy30_registry_entries(repo)
    return text, {app_id: (entry.label, entry.app_class) for app_id, entry in entries.items()}


def check_registry_catalog(catalog: dict, entries: dict[str, RegistryEntry]) -> None:
    """The 21 selectable rows must be exactly the catalogue ports, never its
    six excluded games, and the six built-ins must be exactly those games."""
    built_in = {app_id for app_id, entry in entries.items() if entry.built_in}
    selectable = {app_id for app_id, entry in entries.items() if not entry.built_in}
    catalog_ids = {game["id"] for game in catalog["games"]}
    excluded = set(catalog["existing_games_excluded"])
    if built_in != excluded:
        raise ValueError("Arduboy 30 built-in registry rows must be exactly the catalogue's "
                         f"excluded games: registry={sorted(built_in)} "
                         f"catalogue={sorted(excluded)}")
    if selectable != catalog_ids:
        raise ValueError("Arduboy 30 selectable registry rows must be exactly the 21 "
                         f"catalogue ports: missing={sorted(catalog_ids - selectable)} "
                         f"extra={sorted(selectable - catalog_ids)}")
    for game in catalog["games"]:
        entry = entries[game["id"]]
        if entry.label != game["menu_label"] or entry.app_class != game["app_class"]:
            raise ValueError(f"Arduboy 30 registry metadata mismatch: {game['id']} "
                             f"(catalogue {game['menu_label']!r}/{game['app_class']}, "
                             f"registry {entry.label!r}/{entry.app_class})")


def arduboy30_menu(entries: dict[str, RegistryEntry], selected: list[dict]) -> dict[str, list[str]]:
    """Categorized menu content for this image, for the build report only."""
    enabled = {app_id for app_id, entry in entries.items() if entry.built_in}
    enabled.update(game["id"] for game in selected)
    menu = {category: [] for category in ARDUBOY30_CATEGORIES}
    for app_id, entry in entries.items():
        if app_id in enabled:
            menu[entry.category].append(app_id)
    return {category: ids for category, ids in menu.items() if ids}


def arduboy30_paths(repo: Path, selected: list[dict]) -> list[Path]:
    app = repo / "firmware/rtl8720dn/NanoUI/app"
    if not selected:
        return []
    registry_text, registry = load_arduboy30_registry_entries(repo)
    paths = []
    for game in selected:
        entry = registry.get(game["id"])
        if entry is None or entry.built_in:
            raise ValueError(f"Arduboy 30 port is not registered as selectable in C++: "
                             f"{game['id']}")
        if entry.app_class != game["app_class"]:
            raise ValueError(f"Arduboy 30 app class mismatch: {game['id']} "
                             f"(catalogue {game['app_class']}, registry {entry.app_class})")
        folder = app / "apps/games" / game["id"]
        header = folder / "app.h"
        if not header.is_file():
            raise ValueError(f"Arduboy 30 port directory is incomplete: {game['id']}")
        macro = f"CRABIT_ARDUBOY30_ENABLE_{arduboy30_macro(game['id'])}"
        if f"defined({macro})" not in registry_text:
            raise ValueError(f"Arduboy 30 registry lacks its enable macro: {game['id']}")
        if f'#include "apps/games/{game["id"]}/app.h"' not in registry_text:
            raise ValueError(f"Arduboy 30 registry lacks its app include: {game['id']}")
        for required in ("README.md", "LICENSE"):
            require_file(folder / required)
        license_path = folder / "LICENSE"
        if license_sha256(license_path.read_bytes()) != game["source"]["license_sha256"]:
            raise ValueError(f"Arduboy 30 licence hash mismatch (LF-normalized): {game['id']}")
        paths.append(header)
    return paths


def selector_name(name: str) -> str:
    """Reject anything that is not a C++ identifier: selectors are substrings."""
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)*", name):
        raise ValueError(f"Not a C++ namespace/class identifier: {name!r}")
    return name


def selectors(extra: list[str]) -> tuple[str, ...]:
    names = list(DEFAULT_SELECTORS)
    for name in extra:
        selector_name(name)
        if name not in names:
            names.append(name)
    return tuple(names)


def linked(path: Path) -> bool:
    return path.is_symlink() or getattr(path, "is_junction", lambda: False)()


def validate_output(output: Path, repo: Path, inputs: list[Path]) -> Path:
    output, repo = output.absolute(), repo.resolve()
    # Check lexical ancestors too: resolving a junction must not hide its use.
    if any(linked(p) for p in (output, *output.parents)):
        raise ValueError("Output must not traverse a symlink/junction")
    output = output.resolve()
    if output.exists():
        raise ValueError("Output must be a new, nonexistent directory (no reuse/cleanup)")
    if output == repo or output in repo.parents:
        raise ValueError("Output cannot contain the repository")
    if repo in output.parents and repo / "build" not in output.parents:
        raise ValueError("In-repository output must be below build/")
    for item in inputs:
        item = item.resolve()
        if output == item or item in output.parents or output in item.parents:
            raise ValueError(f"Output overlaps input: {item}")
    return output


def excluded_source(name: str) -> bool:
    name = name.lower()
    return (name == "ble_auth.local.h" or name.endswith(".bak") or
            name in {".git", "__pycache__", "build"} or
            name.startswith(("build_", "build-", "cmake-build")))


def copy_clean(source: Path, target: Path, *, stock_tools: bool = False) -> None:
    def ignore(directory, names):
        relative = Path(directory).relative_to(source)
        ignored = []
        for name in names:
            lower = name.lower()
            generated = stock_tools and (
                (relative == Path(".") and lower.startswith(
                    ("application.", "ram_2", "psram_2", "xip_image2", "km0_", "km4_"))) or
                (relative.as_posix().lower() == "bsp/image" and lower.endswith(".bin")))
            if excluded_source(name) or generated:
                ignored.append(name)
            elif linked(Path(directory) / name):
                raise ValueError(f"Refusing linked input: {Path(directory) / name}")
        return ignored
    shutil.copytree(source, target, ignore=ignore)


def public_header(template: str) -> str:
    pattern = r'^#define CRABIT_BLE_AUTH_CODE "REPLACE"$'
    if len(re.findall(pattern, template, re.M)) != 1:
        raise ValueError("Public auth example no longer matches the expected template")
    return ("// EXPERIMENT ONLY: PUBLIC authentication code 000000; not device-matched.\n" +
            re.sub(pattern, '#define CRABIT_BLE_AUTH_CODE "000000"', template, flags=re.M))


def experimental_profile(text: str) -> str:
    guard = "#define CRABIT_BUILD_PROFILE_H"
    if text.count(guard) != 1:
        raise ValueError("Build profile changed; inspect all-game menu configuration")
    return text.replace(
        guard, guard + "\n#define CRABIT_BW16_ARDUBOY_PORTS 1\n#define CRABIT_BW16_NETWORK_GAME 0", 1)


def arduboy30_link_extras(selected: list[dict]) -> tuple[tuple[str, ...], tuple[tuple[str, str, tuple[str, ...]], ...]]:
    """Return folder selector and exact libm objects.

    The category menu is compiled in every Arduboy-ports image (the six
    built-in games are always rows), so its class is placed in .extflash even
    with an empty catalogue selection. The libm table keeps the existing
    catalogue-selection gate: with no port selected nothing is force-loaded,
    so a default-six image never drags libm members into the extension.
    """
    return (ARDUBOY30_FOLDER_SELECTOR,), (ARDUBOY30_MATH_OBJECTS if selected else ())


def arduboy30_support_selectors() -> tuple[str, ...]:
    """Extra placements for the shared Arduboy support layer in every ports image.

    These are not required symbols: an image may not emit every one of them.
    """
    return tuple(selector_name(name) for name in ARDUBOY30_SUPPORT_SELECTORS)


def arduboy30_math_patterns(
        math_objects: tuple[tuple[str, str, tuple[str, ...]], ...]) -> tuple[str, ...]:
    """Archive-member selectors for GNU ld: '*libm.a:member(sections)'."""
    return tuple(f"*libm.a:{member}({' '.join(sections)})"
                 for member, _symbol, sections in math_objects)


def resource_placements(
        resources: tuple[tuple[str, str], ...] = RESOURCE_SECTIONS) -> tuple[tuple[str, str], ...]:
    """Validate the exact read-only resource table; never a glob or a writable name."""
    seen_sections, seen_symbols = set(), set()
    for symbol, section in resources:
        if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", symbol):
            raise ValueError(f"Not a plain resource symbol: {symbol!r}")
        # Exact `.rodata.<name>` only: no wildcard, no `.text`, no `.data/.bss`.
        if not re.fullmatch(r"\.rodata\.[A-Za-z0-9_.$]+", section):
            raise ValueError(f"Resource placement must be an exact read-only section: {section!r}")
        if symbol in seen_symbols or section in seen_sections:
            raise ValueError(f"Resource listed twice: {symbol}/{section}")
        seen_symbols.add(symbol)
        seen_sections.add(section)
    return tuple(resources)


def resource_patterns(resources: tuple[tuple[str, str], ...] = RESOURCE_SECTIONS) -> tuple[str, ...]:
    """One exact input-section selector per resource, never a substring match."""
    return tuple(f"*({section})" for _symbol, section in resource_placements(resources))


def split_linker(text: str, names: tuple[str, ...],
                 math_objects: tuple[tuple[str, str, tuple[str, ...]], ...] = (),
                 resources: tuple[tuple[str, str], ...] = ()) -> str:
    # GNU section names contain mangled C++ identifiers, including namespace lengths.
    nl = chr(10)
    patterns = []
    for name in names:
        token = name if "::" not in name else "".join(str(len(part)) + part for part in name.split("::"))
        patterns.append(f" *(.text.*{token}* .rodata.*{token}*)")
    for pattern in arduboy30_math_patterns(math_objects):
        patterns.append(" " + pattern)
    # Resources go last so the existing game/math placement order is unchanged.
    for pattern in resource_patterns(resources):
        patterns.append(" " + pattern)
    for anchor in ("MEMORY" + nl + "{", "SECTIONS" + nl + "{"):
        if text.count(anchor) != 1:
            raise ValueError(f"Unexpected SDK linker layout: {anchor!r}")
    text = text.replace("MEMORY" + nl + "{", "MEMORY" + nl + "{" + nl +
                        " EXTFLASH (rx) : ORIGIN = 0x08107000, LENGTH = 0xF1000", 1)
    body = "SECTIONS" + nl + "{" + nl + " .extflash : {" + nl + nl.join(patterns) + nl
    body += " . = ALIGN(16);" + nl + " } > EXTFLASH" + nl
    body += ' ASSERT(SIZEOF(.extflash) <= LENGTH(EXTFLASH), "Extension overflow")' + nl
    return text.replace("SECTIONS" + nl + "{", body, 1)


def check_arduboy30_math(text: str, size: int,
                         math_objects: tuple[tuple[str, str, tuple[str, ...]], ...]) -> list[str]:
    """Verify any pulled whitelist function sits in extension, never in main."""
    found, misplaced = [], []
    for _member, symbol, _sections in math_objects:
        name = re.escape(symbol)
        for line in text.splitlines():
            match = re.fullmatch(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[tTwWrRvV]\s+" + name + r"\s*",
                                 line.strip())
            if not match:
                continue
            address, length = int(match.group(1), 16), int(match.group(2), 16)
            if not length:
                continue
            if XIP_BASE + EXT_OFFSET <= address and address + length <= XIP_BASE + EXT_OFFSET + size:
                found.append(symbol)
            else:
                misplaced.append(symbol)
    if misplaced:
        raise ValueError(f"Arduboy 30 math objects not in extension: {sorted(set(misplaced))}")
    return found


def check_extension(size: int, address: int = XIP_BASE + EXT_OFFSET) -> None:
    if address != XIP_BASE + EXT_OFFSET or not 0 < size <= EXT_END - EXT_OFFSET:
        raise ValueError("Extension outside fixed physical [0x107000, 0x1F8000) range")


def parse_sections(text: str) -> dict[str, tuple[int, int, int]]:
    return {m[1]: tuple(int(x, 16) for x in m.groups()[1:]) for line in text.splitlines()
            if (m := re.match(r"\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\b", line))}


def object_sections(text: str) -> dict[int, str]:
    """Map address -> section for every STT_OBJECT entry in `objdump -t`.

    Only data objects are collected, so a function or an unknown symbol can
    never be excused by this lookup.
    """
    sections = {}
    for line in text.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+.*?\bO\s+(\.\S+)\s+([0-9a-fA-F]+)\s+(.+)$", line)
        if match:
            sections[int(match.group(1), 16)] = match.group(2)
    return sections


def object_symbols(text: str) -> dict[str, list[tuple[int, str, int]]]:
    """Map STT_OBJECT name -> [(address, section, bytes)] from `objdump -t -C`."""
    symbols: dict[str, list[tuple[int, str, int]]] = {}
    for line in text.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+.*?\bO\s+(\S+)\s+([0-9a-fA-F]+)\s+(.+)$", line)
        if match:
            symbols.setdefault(match.group(4).strip(), []).append(
                (int(match.group(1), 16), match.group(2), int(match.group(3), 16)))
    return symbols


def check_resources(text: str, objects: str, size: int,
                    resources: tuple[tuple[str, str], ...] = RESOURCE_SECTIONS) -> list[dict]:
    """Emitted resources must be STT_OBJECT read-only data inside .extflash.

    A reduced image may not emit a resource at all (it is then simply absent),
    but anything that *is* emitted must never stay in the main image, and only
    whole `.rodata.<name>` input sections are ever selected, so writable state
    cannot be moved into flash by this table. An emitted symbol with no length
    is a broken resource, not an absent one, and the objdump object must agree
    with nm on both address and length.
    """
    emitted: dict[str, list[tuple[int, int]]] = {}
    for line in text.splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+(.+)", line.strip())
        if match:
            emitted.setdefault(match.group(4).strip(), []).append(
                (int(match.group(1), 16), int(match.group(2), 16)))
    objects_by_name = object_symbols(objects)
    placed, misplaced, unproved = [], [], []
    for symbol, section in resource_placements(resources):
        spans = emitted.get(symbol, [])
        if not spans:
            continue  # Truly absent from this image; never required.
        for address, length in spans:
            if not length:
                unproved.append(symbol)  # Emitted with no bytes: not "absent".
                continue
            if not (XIP_BASE + EXT_OFFSET <= address and address + length <= XIP_BASE + EXT_OFFSET + size):
                misplaced.append(symbol)
                continue
            evidence = [item for item in objects_by_name.get(symbol, ())
                        if item[0] == address and item[1] == ".extflash" and item[2] == length]
            if not evidence:
                unproved.append(symbol)
                continue
            placed.append({"symbol": symbol, "input_section": section,
                           "output_section": ".extflash", "address": hex(address),
                           "bytes": length})
    if misplaced:
        raise ValueError(f"Read-only resource stayed in main: {sorted(set(misplaced))}")
    if unproved:
        raise ValueError(f"Resource emitted without a real read-only object: {sorted(set(unproved))}")
    return placed


def map_inputs(map_text: str) -> Iterator[tuple[str | None, str, int, int, str]]:
    """Yield (output section, input section, address, size, origin) from a GNU ld map.

    Input-section names with mangled C++ identifiers wrap onto two lines, with
    the address/size/origin on the following line; both shapes are read here.
    Records before any output section belong to "Discarded input sections" and
    keep `output_section=None` so callers can ignore them.
    """
    current, pending = None, None
    for line in map_text.splitlines():
        # Output-section headers sit at column 0 and may wrap their address onto
        # the next line (`.xip_image2.text`), so only the name is taken.
        if line[:1] == "." and re.fullmatch(r"\.[A-Za-z_.][\w.]*", line.split()[0]):
            current, pending = line.split()[0], None
            continue
        if not line[:1].isspace():
            pending = None
            continue
        match = re.match(r"^\s+(\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.*)$", line)
        if match:
            pending = None
            yield (current, match.group(1), int(match.group(2), 16), int(match.group(3), 16),
                   match.group(4))
            continue
        name = re.fullmatch(r"\s+(\.[A-Za-z_][\w.]*)\s*", line)
        if name:
            pending = name.group(1)
            continue
        continuation = re.match(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.*)$", line)
        if continuation and pending:
            yield (current, pending, int(continuation.group(1), 16),
                   int(continuation.group(2), 16), continuation.group(3))
        pending = None


def check_math_members(map_text: str,
                       math_objects: tuple[tuple[str, str, tuple[str, ...]], ...]) -> dict[str, int]:
    """Prove no whitelisted libm member leaves read-only input outside .extflash.

    `nm` only reports named symbols, so the anonymous/duplicated constants of
    __kernel_rem_pio2 (init_jk, PIo2, npio2_hw, two_over_pi) would otherwise be
    invisible. The link map names the archive member for every input section, so
    this checks the whole member, including its read-only tables, and refuses a
    member whose writable sections were pulled into the extension. A discarded
    input (no output section, or address 0) is not placement evidence. A nonempty
    whitelist requires an .extflash output header and real member placements.
    """
    members = {member for member, _symbol, _sections in math_objects}
    if members and not re.search(r"^\.extflash(?:\s|$)", map_text, re.MULTILINE):
        raise ValueError("Missing .extflash output section in math map")
    moved = {}
    for current, section, address, size, origin in map_inputs(map_text):
        if not size or not address or current is None:
            continue  # Discarded input: never part of any output section.
        source = re.search(r"libm\.a\((lib_a-[^)]+)\)", origin)
        if not source or source.group(1) not in members:
            continue
        if not section.startswith((".text", ".rodata", ".data", ".bss", ".ram")):
            continue
        if section.startswith((".text", ".rodata")):
            if current != ".extflash":
                raise ValueError(f"libm member read-only section stayed in {current}: "
                                 f"{source.group(1)} {section}")
            moved[source.group(1)] = moved.get(source.group(1), 0) + size
        elif current == ".extflash":
            raise ValueError(f"libm member writable section moved to extension: "
                             f"{source.group(1)} {section}")
    if members and not moved:
        raise ValueError("No whitelisted libm placement evidence in math map")
    return moved


def check_symbols(text: str, names: tuple[str, ...], size: int, *, asset_names: tuple[str, ...] = (),
                  objects: str = "", optional: tuple[str, ...] = ()) -> None:
    for name in names:
        kinds = set()
        for line in text.splitlines():
            m = re.fullmatch(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([tTwWrRvV])\s+(.+)", line.strip())
            if m and re.search(r"(?<![\w:])" + re.escape(name) + r"(?=::|\b)", m[4]):
                address, length = int(m[1], 16), int(m[2], 16)
                if length and XIP_BASE + EXT_OFFSET <= address and address + length <= XIP_BASE + EXT_OFFSET + size:
                    kinds.add(m[3].lower())
        if not kinds:
            raise ValueError(f"No real extension symbol for namespace/class {name}")
        readonly = {"r"} if name in GAME_NAMESPACES else {"r", "v"}
        # A mixed code/rodata output section makes GNU nm report objects as t/T.
        # Use ELF STT_OBJECT (objdump O flag), not spelling or nm type guesses.
        has_object = False
        for line in objects.splitlines():
            match = re.match(r"^([0-9a-fA-F]+)\s+.*?\bO\s+\.extflash\s+([0-9a-fA-F]+)\s+(.+)$", line)
            if match and re.search(r"(?<![\w:])" + re.escape(name) + r"(?=::|\b)", match[3]):
                address, length = int(match[1], 16), int(match[2], 16)
                has_object |= bool(length and XIP_BASE + EXT_OFFSET <= address and
                                   address + length <= XIP_BASE + EXT_OFFSET + size)
        if name in asset_names and (not kinds.intersection({"t", "w"}) or
                                    not (kinds.intersection(readonly) or has_object)):
            raise ValueError(f"Game namespace needs both real code and readonly assets in extension: {name}")
    # Shared support symbols: absence is fine (a selection may not emit them),
    # but code and readonly data must never stay in the main image. The only
    # exception is mutable RAM state such as the port runtime's lazy singleton:
    # `objdump -t` reports it as STT_OBJECT in .ram_image2.bss, while a weak
    # object left in the read-only xip image still fails (fail closed when the
    # section is unknown, so nm type V alone never excuses a placement).
    misplaced = set()
    allocations = object_sections(objects) if optional else {}
    for name in optional:
        pattern = re.compile(r"(?<![\w:])" + re.escape(name) + r"(?=::|\b)")
        for line in text.splitlines():
            m = re.fullmatch(r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([tTwWrRvV])\s+(.+)", line.strip())
            if not m or not pattern.search(m[4]):
                continue
            address, length = int(m[1], 16), int(m[2], 16)
            if not length:
                continue
            if XIP_BASE + EXT_OFFSET <= address and address + length <= XIP_BASE + EXT_OFFSET + size:
                continue
            if allocations.get(address) in MUTABLE_RAM_SECTIONS:
                continue
            misplaced.add(name)
    if misplaced:
        raise ValueError(f"Arduboy support symbol stayed in main: {sorted(misplaced)}")


def run(args: list, log: Path, *, cwd: Path | None = None) -> str:
    result = subprocess.run([str(x) for x in args], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_bytes(result.stdout)
    text = result.stdout.decode(errors="replace")
    if result.returncode or FAILURE.search(text) or FAILURE.search(result.stdout.decode("gb18030", errors="replace")):
        raise RuntimeError(f"Command failed (exit {result.returncode}); inspect {log}")
    return text


def require_file(path: Path, *, elf: bool = False) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"Missing/empty output or input: {path}")
    if elf:
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                raise ValueError(f"Not an ELF: {path}")


def build(args: argparse.Namespace) -> None:
    sdk = args.sdk.resolve()
    toolchain = (args.toolchain or sdk.parents[2] / "tools/ameba_d_asdk_toolchain/1.0.1/bin").resolve()
    tools = (args.tools or sdk.parents[2] / "tools/ameba_d_tools/1.1.3").resolve()
    source = REPO / "firmware/rtl8720dn/NanoUI"
    requested30 = getattr(args, "arduboy30", [])
    catalog = load_arduboy30_catalog(REPO)
    _registry_text, registry_entries = load_arduboy30_registry_entries(REPO)
    check_registry_catalog(catalog, registry_entries)
    selected = arduboy30_selection(catalog, requested30) if requested30 else []
    arduboy_names = [name for game in selected for name in (game["namespace"], game["app_class"])]
    link_extras, math_objects = arduboy30_link_extras(selected)
    names = selectors([*args.game_namespace, *arduboy_names, *link_extras])
    # Shared game support is placed but not required: a partial selection may
    # not emit every helper, and a missing helper is not a link failure.
    support = tuple(name for name in arduboy30_support_selectors()
                    if name not in names)
    placements = (*names, *support)
    # Product resources are not game namespaces: they keep their own exact
    # section table, are never added to `selectors`, and apply to every split
    # profile (the default six included).
    resources = resource_placements()
    arduboy30_paths(REPO, selected)
    shared = shared_staging(REPO / SHARED_MANIFEST, REPO, source)
    output = validate_output(args.output, REPO, [sdk, toolchain, tools, source, REPO / "libraries"])
    archive = sdk / "variants/common_libs/lib_arduino.a"
    require_file(archive)
    for binary in ("objcopy", "objdump", "nm"):
        require_file(toolchain / f"arm-none-eabi-{binary}.exe")
    require_file(tools / "postbuild_img2_arduino_windows.exe")
    if not shutil.which(str(args.pwsh)):
        raise ValueError("PowerShell executable unavailable; specify --pwsh with an existing executable")
    before = sha256(archive)
    output.mkdir(parents=True)
    report = {"hardware": "NOT TESTED", "flashable": False, "status": "FAIL",
              "selectors": placements, "support_selectors": list(support),
              "resource_sections": [{"symbol": symbol, "input_section": section}
                                    for symbol, section in resources],
              "arduboy30": [game["id"] for game in selected],
              "arduboy30_built_in": sorted(ARDUBOY30_BUILT_IN),
              "arduboy30_menu": arduboy30_menu(registry_entries, selected),
              "public_auth": "000000", "sdk_archive_before": before}
    stage = "staging"
    try:
        sketch = output / "NanoUI"
        copy_clean(source, sketch)
        stage_shared_headers(sketch, shared)
        # Explicit opt-in: the in-tree selection is empty, so only the six
        # built-in games compile; this staged copy additionally enables the
        # selected catalogue ids. The C++ registry owns classes/labels/factories.
        if requested30:
            registry = sketch / "app/arduboy30_selected.h"
            registry.parent.mkdir(parents=True, exist_ok=True)
            registry.write_text(arduboy30_header(selected), encoding="utf-8")
        # Read ONLY the public example, never any existing ble_auth.local.h.
        (sketch / "ble_auth.local.h").write_text(public_header(
            (source / "ble_auth.local.example.h").read_text(encoding="utf-8")), encoding="utf-8")
        profile = sketch / "src/CrabitBuildProfile.h"
        profile.write_text(experimental_profile(profile.read_text(encoding="utf-8")), encoding="utf-8")
        # The sanitized copied sketch is the exact input, including local edits.
        # Never use a Git SHA alone as evidence for an uncommitted source tree.
        manifest = {p.relative_to(sketch).as_posix(): sha256(p)
                    for p in sorted(sketch.rglob("*")) if p.is_file()}
        (output / "sketch-inputs.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        report["sketch_manifest_sha256"] = sha256(output / "sketch-inputs.json")
        linker = output / "split-games.ld"
        linker.write_text(split_linker(
            (sdk / "variants/common_libs/amebad_img2_is_arduino_std.ld").read_text(), placements,
            math_objects, resources))
        platform = (sdk / "platform.txt").read_text()
        recipes = [line.split("=", 1)[1] for line in platform.splitlines() if line.startswith("recipe.c.combine.pattern=")]
        anchor = "{runtime.platform.path}/variants/common_libs/{build.ldscript}"
        if len(recipes) != 1 or recipes[0].count(anchor) != 1:
            raise ValueError("Unexpected SDK link recipe")
        compile_dir = output / "compile"
        stage = "compile"
        run([args.arduino_cli, "compile", "--jobs", "1", "--fqbn", "realtek:AmebaD:Ai-Thinker_BW16",
             "--build-path", compile_dir, "--library", REPO / "libraries/NanoUI", "--library", REPO / "libraries/BW16_BLE_Spam",
             "--build-property", "runtime.platform.path=" + sdk.as_posix(),
             "--build-property", "runtime.tools.ameba_d_asdk_toolchain.path=" + toolchain.parent.as_posix(),
             "--build-property", "compiler.path=" + toolchain.as_posix() + "/",
             "--build-property", "recipe.c.combine.pattern=" + recipes[0].replace(anchor, linker.as_posix()),
             "--build-property", "recipe.objcopy.hex.pattern=cmd /c exit 0",
             "--build-property", "recipe.hooks.objcopy.postobjcopy.pattern.windows=cmd /c exit 0", sketch], output / "compile.log")
        elf = compile_dir / "application.axf"
        require_file(elf, elf=True)
        # The ELF exists and is readable: compile/link really finished, even if
        # a later extension symbol or strict-range check fails.
        report["compile_link"] = "PASS"
        objdump, objcopy, nm = [toolchain / f"arm-none-eabi-{x}.exe" for x in ("objdump", "objcopy", "nm")]
        stage = "sections"
        sections = parse_sections(run([objdump, "-h", elf], output / "sections.log"))
        size, vma, lma = sections[".extflash"]
        check_extension(size, vma)
        check_extension(size, lma)
        report.update(extension_checks="PASS", extension_bytes=size,
                      extension_offset=hex(EXT_OFFSET), extension_end=hex(EXT_OFFSET + size),
                      sections={k: {"bytes": v[0], "vma": hex(v[1]), "lma": hex(v[2])}
                                for k, v in sections.items() if not k.startswith((".debug", ".stab"))})
        stage = "symbols"
        symbol_text = run([nm, "-n", "-S", "-C", elf], output / "symbols.log")
        symbol_types = run([objdump, "-t", "-C", elf], output / "symbol-types.log")
        check_symbols(symbol_text, (*names, *REQUIRED_GAME_LOGIC), size,
                      asset_names=tuple(dict.fromkeys(
                          (*GAME_NAMESPACES, *args.game_namespace, *arduboy_names))),
                      objects=symbol_types,
                      optional=support)
        report["resource_placements"] = check_resources(symbol_text, symbol_types, size, resources)
        report["resource_checks"] = "PASS"
        report["arduboy30_math_symbols"] = check_arduboy30_math(symbol_text, size, math_objects)
        if math_objects:
            map_file = compile_dir / "application.map"
            require_file(map_file)
            report["arduboy30_math_members"] = check_math_members(
                map_file.read_text(encoding="utf-8", errors="replace"), math_objects)
        report["symbol_checks"] = "PASS"
        extension, main_elf = output / "games-extension.bin", output / "main-only.axf"
        stage = "extension_extract"
        run([objcopy, "-O", "binary", "--only-section=.extflash", elf, extension], output / "extract-extension.log")
        require_file(extension)
        if extension.stat().st_size != size:
            raise ValueError("Extension extraction size differs from ELF section")
        stage = "main_only_extract"
        run([objcopy, "--remove-section=.extflash", elf, main_elf], output / "extract-main.log")
        require_file(main_elf, elf=True)
        report["main_only_extract"] = "PASS"
        main_sections = parse_sections(run([objdump, "-h", main_elf], output / "main-sections.log"))
        if main_sections != {k: v for k, v in sections.items() if k != ".extflash"}:
            raise ValueError("Main-only ELF changed sections beyond .extflash removal")
        stage = "stock_pack"
        work = output / "stock-pack"
        copy_clean(tools, work, stock_tools=True)
        pack_input = output / "pack-input"
        pack_input.mkdir()
        shutil.copy2(main_elf, pack_input / "application.axf")
        run([work / "postbuild_img2_arduino_windows.exe", work, pack_input / "application.axf",
             str(toolchain) + "\\", sdk / "variants/rtl8720dn_bw16/{build.sym_bklist}"], output / "packing.log", cwd=work)
        require_file(work / "application.axf", elf=True)
        if sha256(work / "application.axf") != sha256(main_elf) or sha256(pack_input / "application.axf") != sha256(main_elf):
            raise ValueError("Stock packer did not use the main-only ELF")
        image = work / "km0_km4_image2.bin"
        require_file(image)
        report.update(fresh_stock_pack="PASS", main_bytes=image.stat().st_size,
                      main_physical_end=hex(0x6000 + image.stat().st_size))
        stage = "strict_range_check"
        run([args.pwsh, "-NoProfile", "-File", REPO / "tools/check-bw16-image.ps1", "-ImagePath", image,
             "-StrictTarget", "-ManifestPath", output / "main-range.json"], output / "range-check.log")
        report.update(status="PASS", strict_range_check="PASS",
                      main_image=str(image),
                      main_sha256=sha256(image), extension_sha256=sha256(extension), linked_elf_sha256=sha256(elf),
                      main_only_elf_sha256=sha256(main_elf))
    except Exception as error:
        # Keep only facts for stages that really ran; never mark later checks.
        report["failed_stage"] = stage
        report["failure"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        after = sha256(archive)
        report.update(sdk_archive_after=after, original_sdk_archive_unchanged=(before == after))
        if before != after:
            report["status"] = "FAIL"
            report.setdefault("failed_stage", "sdk_archive")
            report.setdefault("failure", "SDK archive changed during experiment")
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if before != after:
            raise RuntimeError("SDK archive changed during experiment; stop and investigate")
    print(json.dumps(report, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, default=DEFAULT_SDK)
    parser.add_argument("--toolchain", type=Path, help="GNU Arm bin directory")
    parser.add_argument("--tools", type=Path, help="Stock ameba_d_tools directory")
    parser.add_argument("--arduino-cli", default=shutil.which("arduino-cli") or
                        str(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Arduino CLI/arduino-cli.exe"))
    parser.add_argument("--pwsh", default=shutil.which("pwsh") or shutil.which("powershell.exe") or "pwsh",
                        help="PowerShell executable with Get-FileHash (pwsh preferred)")
    parser.add_argument("--game-namespace", action="append", default=[], help="Additional namespace/class (repeatable)")
    parser.add_argument("--arduboy30", action="append", default=[],
                        help="Catalogue id to include, or 'all' for every verified port (repeatable)")
    build(parser.parse_args())


if __name__ == "__main__":
    main()
