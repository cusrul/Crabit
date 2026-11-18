"""Contract tests for the shareable BW16 app package layout."""
import importlib.util
import io
import re
import shutil
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("bw16_apps", ROOT / "tools/bw16_apps.py")
apps = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(apps)

FINAL_IDS = """arduboy3d castle_boy hopper hollow lasers samegame
ardubullets bananonsense chiemagari chribocchi evasion knightmove
ardugo minesweeper game2048 spacetanks galaxion stairssweep toyokumono rysk
reversi quarto asteroiddodge memoryrhythm pong jumpboy longcat""".split()
BUILT_IN = {"arduboy3d", "castle_boy", "hopper", "hollow", "lasers", "samegame"}


def registry_text(entries):
    """Emit the same row shape the real registry uses, in source order."""
    lines = ["#ifndef CRABIT_ARDUBOY30_REGISTRY_H", "#define CRABIT_ARDUBOY30_REGISTRY_H"]
    for app_id, label, app_class in entries:
        macro = re.sub(r"[^A-Za-z0-9]", "_", app_id).upper()
        if app_id in BUILT_IN:
            lines.append(f'#include "apps/games/{app_id}/app.h"')
        else:
            lines += [f'#if defined(CRABIT_ARDUBOY30_ENABLE_{macro})',
                      f'#include "apps/games/{app_id}/app.h"', "#endif"]
    lines.append("static const Entry table[] = {")
    for app_id, label, app_class in entries:
        macro = re.sub(r"[^A-Za-z0-9]", "_", app_id).upper()
        if app_id in BUILT_IN:
            lines += [f'    builtInEntryFor<{app_class}>("{app_id}", "{label}", '
                      f"Category::Puzzle),"]
        else:
            lines += [f'#if defined(CRABIT_ARDUBOY30_ENABLE_{macro})',
                      f'    entryFor<{app_class}>("{app_id}", "{label}", Category::Puzzle),',
                      "#endif"]
    lines += ["    {nullptr, nullptr, nullptr, Category::Count}", "};", "#endif", ""]
    return "\n".join(lines)


def make_package(root, app_id="good", app_class="GoodApp", label="Good"):
    package = root / "apps/games" / app_id
    package.mkdir(parents=True)
    (package / "app.h").write_text(
        f"#ifndef APP_GOOD_H\n#define APP_GOOD_H\nclass {app_class} {{}};\n#endif\n",
        encoding="utf-8")
    (package / "README.md").write_text(f"# {label}\n\nMIT; frame; arduboy2-compat.\n",
                                      encoding="utf-8")
    (package / "LICENSE").write_text("MIT\n", encoding="utf-8")
    return package


class Bw16AppsTests(unittest.TestCase):
    def temp_root(self, entries=(("good", "Good", "GoodApp"),)):
        directory = tempfile.TemporaryDirectory(prefix="crabit-apps-")
        self.addCleanup(directory.cleanup)
        root = Path(directory.name) / "app"
        (root / "apps").mkdir(parents=True)
        shutil.copytree(ROOT / "firmware/rtl8720dn/NanoUI/app/apps/_template",
                        root / "apps/_template")
        (root / apps.REGISTRY_NAME).write_text(registry_text(entries), encoding="utf-8")
        make_package(root)
        return root

    def test_repository_registration_and_packages_validate(self):
        data = apps.validate(ROOT / "firmware/rtl8720dn/NanoUI/app")
        self.assertEqual(len(data), 27)
        self.assertEqual({entry["id"] for entry in data}, set(FINAL_IDS))
        self.assertEqual({entry["id"] for entry in data if entry["built_in"]}, BUILT_IN)
        self.assertEqual({entry["category"] for entry in data},
                         set(apps.GAMEPLAY_CATEGORIES))
        counts = {}
        for entry in data:
            counts[entry["category"]] = counts.get(entry["category"], 0) + 1
        self.assertEqual(counts, {"Shooter": 4, "JumpAction": 10, "Puzzle": 8,
                                  "Board": 3, "CasualRhythm": 2})
        self.assertNotIn("_template", {entry["id"] for entry in data})
        self.assertFalse({entry["id"] for entry in data} & apps.PARTIAL_PACKAGES)
        self.assertFalse(list((ROOT / "firmware/rtl8720dn/NanoUI/app/apps").rglob("app.json")))
        for entry in data:
            package = apps.resolve_package(ROOT / "firmware/rtl8720dn/NanoUI/app", entry["id"])
            self.assertTrue((package / "README.md").is_file())
            self.assertTrue(apps.license_files(package))

    def test_scaffold_creates_one_class_and_requires_one_registration(self):
        root = self.temp_root()
        shutil.rmtree(root / "apps/games/good")
        (root / apps.REGISTRY_NAME).write_text("#define CRABIT_ARDUBOY30_COUNT 0\n",
                                               encoding="utf-8")
        with redirect_stdout(io.StringIO()) as output:
            self.assertEqual(apps.scaffold(root, "games", "my-game"), 0)
        target = root / "apps/games/my-game"
        self.assertFalse((target / "app.json").exists())
        self.assertIn("class MyGameApp", (target / "app.h").read_text(encoding="utf-8"))
        self.assertIn('#include "apps/games/my-game/app.h"', output.getvalue())
        self.assertIn('entryFor<MyGameApp>("my-game", "my-game", Category::<gameplay>)',
                      output.getvalue())
        for category in sorted(apps.GAMEPLAY_CATEGORIES):
            self.assertIn(category, output.getvalue())
        with redirect_stdout(io.StringIO()):
            with self.assertRaises(apps.ValidationError):
                apps.validate(root)  # not registered yet
        (root / apps.REGISTRY_NAME).write_text(
            registry_text([("my-game", "my-game", "MyGameApp")]), encoding="utf-8")
        data = apps.validate(root)
        self.assertEqual([(entry["id"], entry["class"]) for entry in data],
                         [("my-game", "MyGameApp")])

    def test_scaffold_non_game_category_does_not_claim_registry_support(self):
        root = self.temp_root()
        with redirect_stdout(io.StringIO()) as output:
            self.assertEqual(apps.scaffold(root, "wireless", "probe"), 0)
        self.assertIn("not enabled", output.getvalue())
        self.assertNotIn("entryFor<", output.getvalue())
        self.assertFalse((root / "apps/wireless/probe/app.json").exists())

    def test_duplicate_registration_is_rejected(self):
        root = self.temp_root([("good", "Good", "GoodApp"), ("good", "Other", "OtherApp")])
        with self.assertRaisesRegex(apps.ValidationError, "duplicate app id"):
            apps.validate(root)
        root = self.temp_root([("good", "Good", "GoodApp"), ("other", "Good", "OtherApp")])
        with self.assertRaisesRegex(apps.ValidationError, "duplicate menu label"):
            apps.validate(root)
        root = self.temp_root([("good", "Good", "GoodApp"), ("other", "Other", "GoodApp")])
        with self.assertRaisesRegex(apps.ValidationError, "duplicate app class"):
            apps.validate(root)

    def test_rejects_auto_compiled_source_and_absolute_paths(self):
        root = self.temp_root()
        package = root / "apps/games/good"
        (package / "compiled.cpp").write_text("int x;\n", encoding="utf-8")
        with self.assertRaises(apps.ValidationError):
            apps.validate(root)
        (package / "compiled.cpp").unlink()
        (package / "src").mkdir()
        (package / "src/compiled.cpp").write_text("int x;\n", encoding="utf-8")
        with self.assertRaises(apps.ValidationError):
            apps.validate(root)
        (package / "src/compiled.cpp").unlink()
        (package / "ble_auth.local.h").write_text("secret\n", encoding="utf-8")
        with self.assertRaises(apps.ValidationError):
            apps.validate(root)
        (package / "ble_auth.local.h").unlink()
        (package / "README.md").write_text("C:\\Users\\someone\\notes\n", encoding="utf-8")
        with self.assertRaises(apps.ValidationError):
            apps.validate(root)
        (package / "README.md").write_text("# Good\n", encoding="utf-8")
        (package / "app.json").write_text("{}\n", encoding="utf-8")
        with self.assertRaisesRegex(apps.ValidationError, "app.json is retired"):
            apps.validate(root)

    def test_unregistered_and_partial_packages_are_not_registered(self):
        root = self.temp_root()
        make_package(root, "extra", "ExtraApp", "Extra")
        with self.assertRaisesRegex(apps.ValidationError, "not registered"):
            apps.validate(root)
        shutil.rmtree(root / "apps/games/extra")
        partial = root / "apps/games/waternet"
        partial.mkdir()
        (partial / "app.h").write_text("// partial\n", encoding="utf-8")
        # Root .cpp may remain in an incomplete retained port, but sensitive
        # layout mistakes are still rejected without touching the package.
        (partial / "compiled.cpp").write_text("int x;\n", encoding="utf-8")
        self.assertEqual([entry["id"] for entry in apps.validate(root)], ["good"])
        (partial / "ble_auth.local.h").write_text("secret\n", encoding="utf-8")
        with self.assertRaisesRegex(apps.ValidationError, "private BLE"):
            apps.validate(root)
        (partial / "ble_auth.local.h").unlink()
        (partial / "bad.inc").write_text("// not staged\n", encoding="utf-8")
        with self.assertRaisesRegex(apps.ValidationError, r"\.inc is not copied"):
            apps.validate(root)
        (partial / "bad.inc").unlink()
        # Completing a retained partial still must not promote/register it.
        (partial / "README.md").write_text("# Waternet\n", encoding="utf-8")
        (partial / "LICENSE").write_text("MIT\n", encoding="utf-8")
        self.assertEqual([entry["id"] for entry in apps.validate(root)], ["good"])
        (root / apps.REGISTRY_NAME).write_text(
            registry_text([("good", "Good", "GoodApp"),
                           ("waternet", "Waternet", "WaternetApp")]), encoding="utf-8")
        with self.assertRaisesRegex(apps.ValidationError, "partial package must not be registered"):
            apps.validate(root)
        shutil.rmtree(partial)
        incomplete = root / "apps/games/incomplete"
        incomplete.mkdir()
        (incomplete / "app.h").write_text("// incomplete\n", encoding="utf-8")
        (root / apps.REGISTRY_NAME).write_text(registry_text([("good", "Good", "GoodApp")]),
                                               encoding="utf-8")
        with self.assertRaisesRegex(apps.ValidationError, "incomplete package"):
            apps.validate(root)

    def test_non_game_package_is_checked_but_not_in_the_game_registry(self):
        root = self.temp_root()
        package = root / "apps/wireless/probe"
        package.mkdir(parents=True)
        (package / "app.h").write_text("class ProbeApp {};\n", encoding="utf-8")
        (package / "README.md").write_text("# Probe\n", encoding="utf-8")
        (package / "LICENSE").write_text("MIT\n", encoding="utf-8")
        self.assertEqual([entry["id"] for entry in apps.validate(root)], ["good"])
        (package / "raw.cpp").write_text("int x;\n", encoding="utf-8")
        with self.assertRaises(apps.ValidationError):
            apps.validate(root)

    def test_built_in_rows_and_categories_are_validated(self):
        root = self.temp_root()
        registry = root / apps.REGISTRY_NAME
        good = registry_text([("good", "Good", "GoodApp")])
        # 'good' is not one of the six built-in ids, so this form must be
        # rejected: it would make an arbitrary game always present.
        registry.write_text(good.replace("entryFor<GoodApp>", "builtInEntryFor<GoodApp>"),
                            encoding="utf-8")
        with self.assertRaisesRegex(apps.ValidationError, "does not match app id"):
            apps.validate(root)
        # A built-in id must use builtInEntryFor, so a selectable row cannot
        # silently replace an always-present game.
        package = root / "apps/games/hollow"
        package.mkdir()
        (package / "app.h").write_text("class HollowSeekerApp {};\n", encoding="utf-8")
        (package / "README.md").write_text("# Hollow\n", encoding="utf-8")
        (package / "LICENSE").write_text("MIT\n", encoding="utf-8")
        registry.write_text(
            good.replace('entryFor<GoodApp>("good", "Good", Category::Puzzle)',
                         'entryFor<GoodApp>("good", "Good", Category::Puzzle),\n'
                         '    entryFor<HollowSeekerApp>("hollow", "Hollow", '
                         "Category::JumpAction)"),
            encoding="utf-8")
        with self.assertRaisesRegex(apps.ValidationError, "does not match app id"):
            apps.validate(root)
        shutil.rmtree(package)
        # Unknown gameplay categories are rejected, not silently listed.
        registry.write_text(good.replace("Category::Puzzle", "Category::Platformer"),
                            encoding="utf-8")
        with self.assertRaisesRegex(apps.ValidationError, "unknown gameplay category"):
            apps.validate(root)
        registry.write_text(good, encoding="utf-8")
        self.assertEqual([entry["id"] for entry in apps.validate(root)], ["good"])


if __name__ == "__main__":
    unittest.main()
