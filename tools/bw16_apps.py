"""Validate and scaffold shareable BW16 App packages.

This tool never builds firmware, touches a device or installs binary plugins.
It checks the source-package contract under app/apps/ and the single C++ menu
registration in app/arduboy30_registry.h: 27 games in five gameplay categories
(six built-in games plus the 21 catalogue ports), with no old/new split.
Packages no longer carry app.json: id, menu label, class, category and factory
are registered once in C++.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import sys

REPO = Path(__file__).resolve().parents[1]
DEFAULT_APP_ROOT = REPO / "firmware/rtl8720dn/NanoUI/app"
REGISTRY_NAME = "arduboy30_registry.h"
CATEGORIES = {"games", "wireless", "system"}
GAMEPLAY_CATEGORIES = frozenset({"Shooter", "JumpAction", "Puzzle", "Board", "CasualRhythm"})
# Games present in every Arduboy-ports image: `builtInEntryFor`, no selection
# macro. Must equal the catalogue's existing_games_excluded.
BUILT_IN_GAMES = frozenset({
    "arduboy3d", "castle_boy", "hopper", "hollow", "lasers", "samegame",
})
ID_RE = re.compile(r"^[a-z0-9]+(?:[-_][a-z0-9]+)*$")
CLASS_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
ABSOLUTE_PATH = re.compile(r"(?<![A-Za-z0-9])[A-Za-z]:[\\/]|^\\\\[A-Za-z0-9_.-]+[\\/]", re.M)
REGISTRY_ENTRY = re.compile(
    r'(?P<kind>builtInEntryFor|entryFor)<(?P<app_class>[A-Za-z_][A-Za-z0-9_]*)>'
    r'\(\s*"(?P<app_id>[a-z0-9]+(?:[-_][a-z0-9]+)*)"\s*,\s*'
    r'"(?P<label>(?:[^"\\]|\\.)*)"\s*,\s*Category::(?P<category>[A-Za-z_][A-Za-z0-9_]*)\s*\)')
# Internal C++ still at package roots; new packages must use .inc.h bodies.
LEGACY_CPP_LAYOUT = frozenset({"games/arduboy3d", "games/castle_boy"})
# Kept incomplete on purpose; they must never be treated as registered or
# usable. Root .cpp may remain, but private BLE headers and .inc are still
# rejected by the non-destructive source checks.
PARTIAL_PACKAGES = frozenset({
    "games/ardulo", "games/evade", "games/montana",
    "games/slidingtiles", "games/waternet",
})


class ValidationError(ValueError):
    pass


def app_root(value: str | Path | None) -> Path:
    return Path(value).resolve() if value else DEFAULT_APP_ROOT


def packages(root: Path) -> list[Path]:
    apps = root / "apps"
    if not apps.is_dir():
        raise ValidationError(f"Missing apps directory: {apps}")
    return sorted(path for path in apps.glob("*/*")
                  if path.is_dir() and path.name != "_template")


def package_name(root: Path, folder: Path) -> str:
    return folder.relative_to(root / "apps").as_posix()


def read_registry(root: Path) -> tuple[str, list[dict]]:
    path = root / REGISTRY_NAME
    if not path.is_file():
        raise ValidationError(f"Missing single C++ registry: {path}")
    text = path.read_text(encoding="utf-8")
    entries, seen_ids, seen_classes, seen_labels, seen_macros = [], set(), set(), set(), set()
    for match in REGISTRY_ENTRY.finditer(text):
        kind = match["kind"]
        app_class = match["app_class"]
        app_id = match["app_id"]
        label = match["label"]
        category = match["category"]
        built_in = kind == "builtInEntryFor"
        if category not in GAMEPLAY_CATEGORIES:
            raise ValidationError(f"{path}: unknown gameplay category {category!r} "
                                  f"for {app_id!r}")
        # The row kind is selection policy, not age: built-in rows compile in
        # every ports image, selectable rows need their staged macro. It must
        # agree with the canonical id set so neither kind absorbs the other.
        if built_in != (app_id in BUILT_IN_GAMES):
            raise ValidationError(
                f"{path}: {kind} does not match app id {app_id!r} "
                "(the six built-in ids use builtInEntryFor, the rest entryFor)")
        if app_id in seen_ids:
            raise ValidationError(f"{path}: duplicate app id {app_id!r}")
        if app_class in seen_classes:
            raise ValidationError(f"{path}: duplicate app class {app_class!r}")
        if label in seen_labels:
            raise ValidationError(f"{path}: duplicate menu label {label!r}")
        macro = re.sub(r"[^A-Za-z0-9]", "_", app_id).upper()
        if macro in seen_macros:
            raise ValidationError(f"{path}: enable macro collision for {app_id!r}")
        seen_ids.add(app_id)
        seen_classes.add(app_class)
        seen_labels.add(label)
        seen_macros.add(macro)
        entries.append({"id": app_id, "name": label, "class": app_class,
                        "category": category, "built_in": built_in})
    if not entries:
        raise ValidationError(f"{path}: no app registration found")
    for entry in entries:
        if not re.search(r'#include "apps/games/' + re.escape(entry["id"]) + r'/app\.h"',
                         text):
            raise ValidationError(f"{path}: {entry['id']!r} lacks its app.h include")
        if entry["built_in"]:
            continue
        macro = re.sub(r"[^A-Za-z0-9]", "_", entry["id"]).upper()
        if f"defined(CRABIT_ARDUBOY30_ENABLE_{macro})" not in text:
            raise ValidationError(f"{path}: {entry['id']!r} lacks its enable macro")
    return text, entries


def validate_source_tree(folder: Path, allow_cpp: bool = False) -> None:
    if (folder / "app.json").is_file():
        raise ValidationError(f"{folder}: app.json is retired; register the app in C++ once")
    for path in folder.rglob("*"):
        if not path.is_file():
            continue
        if path.name.lower() == "ble_auth.local.h":
            raise ValidationError(f"{path}: private BLE header is not allowed")
        relative = path.relative_to(folder)
        if not allow_cpp and path.suffix.lower() in {".cpp", ".ino"} and (
            len(relative.parts) == 1 or relative.parts[0].lower() == "src"
        ):
            raise ValidationError(f"{path}: Arduino would automatically compile this; use .inc.h")
        if path.suffix.lower() == ".inc":
            raise ValidationError(f"{path}: .inc is not copied by Arduino staging; use .inc.h")
        if path.suffix.lower() in {".h", ".md", ".json", ".txt", ".license"}:
            text = path.read_text(encoding="utf-8", errors="replace")
            if ABSOLUTE_PATH.search(text):
                raise ValidationError(f"{path}: contains an absolute machine path")


def package_name_from_folder(folder: Path) -> str:
    return f"{folder.parent.name}/{folder.name}"


def license_files(folder: Path) -> list[Path]:
    return [path for path in folder.iterdir()
            if path.is_file() and path.name.upper().startswith("LICENSE")]


def validate_package(folder: Path, registered: dict | None = None) -> None:
    name = package_name_from_folder(folder)
    if not (folder / "app.h").is_file():
        raise ValidationError(f"{folder}: missing app.h entry")
    if not (folder / "README.md").is_file():
        raise ValidationError(f"{folder}: missing README.md")
    if not license_files(folder):
        raise ValidationError(f"{folder}: missing LICENSE")
    allow_cpp = name in LEGACY_CPP_LAYOUT
    validate_source_tree(folder, allow_cpp=allow_cpp)
    if registered is not None:
        header = (folder / "app.h").read_text(encoding="utf-8", errors="replace")
        if not re.search(r"\bclass\s+" + re.escape(registered["class"]) + r"\b", header):
            raise ValidationError(f"{folder}: registered class {registered['class']} not found in app.h")


def resolve_package(root: Path, app_id: str) -> Path | None:
    """Registry ids are bare game ids; other categories are not in this table."""
    games = root / "apps/games"
    if not games.is_dir():
        return None
    matches = [path for path in games.iterdir() if path.is_dir() and path.name == app_id]
    if len(matches) > 1:
        raise ValidationError(f"App id {app_id!r} exists more than once under games/")
    return matches[0] if matches else None


def validate(root: Path) -> list[dict]:
    """Return registered game entries after checking packages and C++ safety rules."""
    _text, entries = read_registry(root)
    by_id = {entry["id"]: entry for entry in entries}
    complete = set()
    for folder in packages(root):
        name = package_name(root, folder)
        is_game = folder.parent.name == "games"
        registered = by_id.get(folder.name) if is_game else None
        # Any registration of a retained partial package is promotion, which is
        # forbidden until the package is completed deliberately.
        if name in PARTIAL_PACKAGES and registered is not None:
            raise ValidationError(f"{folder}: partial package must not be registered")
        if name in PARTIAL_PACKAGES:
            # Do not claim it is usable, but still reject private BLE headers,
            # .inc files and absolute paths. Root .cpp may remain because the
            # incomplete port is not staged or registered.
            validate_source_tree(folder, allow_cpp=True)
            continue
        has_entry = bool((folder / "app.h").is_file() and (folder / "README.md").is_file()
                         and license_files(folder))
        if not has_entry:
            raise ValidationError(f"{folder}: incomplete package lacks app.h/README/LICENSE")
        complete.add(name)
        validate_package(folder, registered)
        # Every complete game package is in the one registry now, including the
        # six built-in games; there is no legacy unregistered set. Wireless and
        # system packages are safety-checked but are not game menu rows.
        if is_game and folder.name not in by_id:
            raise ValidationError(f"{folder}: package is not registered in {REGISTRY_NAME}")
    for app_id in by_id:
        folder = resolve_package(root, app_id)
        if folder is None:
            raise ValidationError(f"{REGISTRY_NAME}: registered app has no game package: {app_id!r}")
        if package_name(root, folder) not in complete:
            raise ValidationError(f"{folder}: registered app needs app.h/README/LICENSE")
    return entries


def command_list(root: Path) -> int:
    entries = validate(root)
    print("Registered games in the C++ registry, by gameplay category "
          "(other installed packages are checked but not listed):")
    for entry in entries:
        folder = resolve_package(root, entry["id"])
        kind = "built-in" if entry["built_in"] else "selectable"
        print(f"{entry['id']:20s} {entry['category']:13s} {entry['class']:24s} "
              f"{kind:10s} {package_name(root, folder)}")
    return 0


def command_validate(root: Path) -> int:
    try:
        entries = validate(root)
    except ValidationError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    categories = sorted({entry["category"] for entry in entries})
    built_in = sorted(entry["id"] for entry in entries if entry["built_in"])
    print(f"App validation passed: {len(entries)} registered game(s) in "
          f"{len(categories)} gameplay categor{'y' if len(categories) == 1 else 'ies'} "
          f"({', '.join(categories)}); built-in: {', '.join(built_in) or 'none'}; "
          f"{len(PARTIAL_PACKAGES)} retained partial package(s) not registered; "
          "other installed packages are safety-checked but not in this registry")
    return 0


def scaffold(root: Path, category: str, slug: str) -> int:
    if category not in CATEGORIES:
        print(f"Unknown category: {category}", file=sys.stderr)
        return 1
    if not ID_RE.fullmatch(slug):
        print(f"Invalid app id: {slug}", file=sys.stderr)
        return 1
    source = root / "apps/_template"
    target = root / "apps" / category / slug
    if target.exists():
        print(f"Target already exists: {target}", file=sys.stderr)
        return 1
    if not source.is_dir():
        print(f"Missing template: {source}", file=sys.stderr)
        return 1
    shutil.copytree(source, target)
    class_name = "".join(part.capitalize() for part in slug.replace("_", "-").split("-")) + "App"
    guard = f"CRABIT_APP_{slug.upper().replace('-', '_')}_H"
    header = (target / "app.h").read_text(encoding="utf-8")
    header = header.replace("CRABIT_APP_EXAMPLE_H", guard)
    header = header.replace("ExampleApp", class_name).replace('"Example App"', f'"{slug}"')
    (target / "app.h").write_text(header, encoding="utf-8")
    readme = (target / "README.md").read_text(encoding="utf-8")
    (target / "README.md").write_text(
        readme.replace("# Example App", f"# {slug}"), encoding="utf-8")
    print(f"Created {target}")
    if category != "games":
        print("This package is outside the 27-game C++ registry; it is not enabled or")
        print("listed by the game menu. Decide launcher/profile integration separately.")
        return 0
    macro = f"CRABIT_ARDUBOY30_ENABLE_{slug.upper().replace('-', '_')}"
    print("Add one C++ registration in app/arduboy30_registry.h, in both blocks:")
    print(f"    #if defined({macro})")
    print(f'    #include "apps/games/{slug}/app.h"')
    print("    #endif")
    print("    ...")
    print(f"    #if defined({macro})")
    print(f'        entryFor<{class_name}>("{slug}", "{slug}", Category::<gameplay>),')
    print("    #endif")
    print("Gameplay category is one of: " + ", ".join(sorted(GAMEPLAY_CATEGORIES)) + ".")
    print("Selection wiring for the enable macro is separate from this registration.")
    print("Then run: python tools/bw16_apps.py validate")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-root", type=Path, help="app directory (default: repository BW16 app)")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list", help="list registered app packages")
    sub.add_parser("validate", help="validate packages and C++ registration")
    scaffold_parser = sub.add_parser("scaffold", help="create a new app package from the template")
    scaffold_parser.add_argument("category", choices=sorted(CATEGORIES))
    scaffold_parser.add_argument("slug")
    args = parser.parse_args(argv)
    root = app_root(args.app_root)
    if args.command == "list":
        return command_list(root)
    if args.command == "validate":
        return command_validate(root)
    return scaffold(root, args.category, args.slug)


if __name__ == "__main__":
    raise SystemExit(main())
