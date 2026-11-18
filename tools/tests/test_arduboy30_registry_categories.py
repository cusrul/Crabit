"""Categorized 27-game registry: real C++ interface, contents and menu wiring.

Host-only. No embedded build, SDK, device or serial access is used here.
"""
import importlib.util
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / "firmware/rtl8720dn/NanoUI"
APPS = SKETCH / "app/apps"
GAMES = APPS / "games"
REGISTRY = SKETCH / "app/arduboy30_registry.h"
LAUNCHER = SKETCH / "app/core/launcher.h"
WIDGET = ROOT / "libraries/NanoUI/src/NanoUI/product/framework/widget.h"

CATEGORY_ORDER = ("Shooter", "JumpAction", "Puzzle", "Board", "CasualRhythm")
CATEGORY_LABELS = ("射击", "动作跳跃", "益智解谜", "棋类对弈", "休闲节奏")
# Single source of expected content: (id, menu label, class, category).
EXPECTED_ROWS = [
    # Shooter (4)
    ("ardubullets", "ARDUBULLET", "ArdubulletsApp", "Shooter"),
    ("galaxion", "Galaxion", "GalaxionApp", "Shooter"),
    ("spacetanks", "Space Tanks", "SpaceTanksApp", "Shooter"),
    ("arduboy3d", "Arduboy 3D", "Arduboy3DApp", "Shooter"),
    # Jump/Action (10)
    ("castle_boy", "Castle Boy", "CastleBoyApp", "JumpAction"),
    ("jumpboy", "JumpBoy", "JumpBoyApp", "JumpAction"),
    ("hopper", "Hopper", "AppHopper", "JumpAction"),
    ("hollow", "Hollow Seeker", "HollowSeekerApp", "JumpAction"),
    ("lasers", "Lasers", "LasersApp", "JumpAction"),
    ("evasion", "Evasion", "EvasionApp", "JumpAction"),
    ("chribocchi", "Chri-Bocchi Cat", "ChribocchiApp", "JumpAction"),
    ("rysk", "RYSK", "RyskApp", "JumpAction"),
    ("toyokumono", "Toyokumono", "ToyokumonoApp", "JumpAction"),
    ("asteroiddodge", "Asteroid Dodge", "AsteroidDodgeApp", "JumpAction"),
    # Puzzle (8)
    ("samegame", "SameGame", "SameGameApp", "Puzzle"),
    ("bananonsense", "Bananonsense", "BananonsenseApp", "Puzzle"),
    ("chiemagari", "Chie Magari Ita", "ChiemagariApp", "Puzzle"),
    ("minesweeper", "Minesweeper", "MinesweeperApp", "Puzzle"),
    ("game2048", "2048", "Game2048App", "Puzzle"),
    ("stairssweep", "Stairssweep", "StairssweepApp", "Puzzle"),
    ("longcat", "LongCat", "LongCatApp", "Puzzle"),
    ("knightmove", "Knight Move", "KnightMoveApp", "Puzzle"),
    # Board (3)
    ("ardugo", "ArduGo", "ArduGoApp", "Board"),
    ("reversi", "Reversi", "ReversiApp", "Board"),
    ("quarto", "Quarto!", "QuartoApp", "Board"),
    # Casual/Rhythm (2)
    ("pong", "Pong", "PongApp", "CasualRhythm"),
    ("memoryrhythm", "Memory Rhythm", "MemoryRhythmApp", "CasualRhythm"),
]
BUILT_IN = {"arduboy3d", "castle_boy", "hopper", "hollow", "lasers", "samegame"}
EXPECTED_COUNTS = {"Shooter": 4, "JumpAction": 10, "Puzzle": 8, "Board": 3,
                   "CasualRhythm": 2}


def builder():
    spec = importlib.util.spec_from_file_location(
        "bw16_split", ROOT / "tools/build_bw16_split_games.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def preprocess(source, flags=()):
    """Strip includes and expand the actual conditionals with the host compiler."""
    compiler = shutil.which("g++")
    if not compiler:
        raise unittest.SkipTest("host g++ is required")
    body = re.sub(r"^\s*#include[^\n]*", "", source, flags=re.M)
    return subprocess.run([compiler, "-E", "-P", "-x", "c++", *flags, "-"],
                          input=body, text=True, capture_output=True, cwd=ROOT)


def read(path):
    return path.read_text(encoding="utf-8")


def menu_source():
    """config.h + profile + launcher, as the embedded translation unit sees them."""
    return "\n".join(read(path) for path in (
        SKETCH / "config.h", SKETCH / "src/CrabitBuildProfile.h", LAUNCHER))


class CategorizedRegistryTests(unittest.TestCase):
    def setUp(self):
        self.build = builder()

    def test_registry_rows_are_the_27_expected_games(self):
        text, entries = self.build.load_arduboy30_registry_entries(ROOT)
        rows = [(app_id, entry.label, entry.app_class, entry.category)
                for app_id, entry in entries.items()]
        self.assertEqual(rows, EXPECTED_ROWS)
        self.assertEqual(len(entries), 27)
        self.assertEqual({app_id for app_id, entry in entries.items() if entry.built_in},
                         BUILT_IN)
        for category, expected in EXPECTED_COUNTS.items():
            self.assertEqual(
                sum(1 for _id, _label, _class, cat in rows if cat == category), expected,
                category)
        self.assertEqual(sum(EXPECTED_COUNTS.values()), 27)
        self.assertEqual({category for _id, _label, _class, category in rows},
                         set(CATEGORY_ORDER))
        # Ids, labels and factories are unique across the one table.
        self.assertEqual(len({row[0].casefold() for row in rows}), 27)
        self.assertEqual(len({row[1] for row in rows}), 27)
        self.assertEqual(len({row[2] for row in rows}), 27)
        # No old/new split survives in the C++ registration.
        self.assertNotIn("Arduboy 30", text)
        self.assertNotIn("Arduboy30FolderApp", text)
        self.assertIn("enum class Category : uint8_t", text)
        self.assertIn("template <typename T>", text)

    def test_catalogue_and_built_ins_are_not_conflated(self):
        catalog = self.build.load_arduboy30_catalog(ROOT)
        _text, entries = self.build.load_arduboy30_registry_entries(ROOT)
        self.build.check_registry_catalog(catalog, entries)
        self.assertEqual({game["id"] for game in catalog["games"]},
                         {app_id for app_id, entry in entries.items() if not entry.built_in})
        self.assertEqual(set(catalog["existing_games_excluded"]) & BUILT_IN, BUILT_IN)
        # A catalogue port cannot be promoted to built-in, and a built-in game
        # cannot be demoted to a selectable row.
        promoted = dict(entries)
        promoted["pong"] = self.build.RegistryEntry("Pong", "PongApp", "CasualRhythm", True)
        with self.assertRaisesRegex(ValueError, "built-in registry rows"):
            self.build.check_registry_catalog(catalog, promoted)
        demoted = dict(entries)
        demoted["hollow"] = self.build.RegistryEntry(
            "Hollow Seeker", "HollowSeekerApp", "JumpAction", False)
        with self.assertRaisesRegex(ValueError, "built-in registry rows"):
            self.build.check_registry_catalog(catalog, demoted)
        # The selectable set is exactly 21, so a registry row can never be read
        # as "27 catalogue ports".
        self.assertEqual(len(catalog["games"]), 21)
        self.assertEqual(len(entries), 27)
        wrong_label = dict(entries)
        wrong_label["pong"] = self.build.RegistryEntry("Pong!", "PongApp", "CasualRhythm", False)
        with self.assertRaisesRegex(ValueError, "metadata mismatch"):
            self.build.check_registry_catalog(catalog, wrong_label)

    def test_each_game_folder_exists_once_and_matches_the_registration(self):
        _text, entries = self.build.load_arduboy30_registry_entries(ROOT)
        folders = {path.name.casefold(): path for path in GAMES.iterdir() if path.is_dir()}
        # Every registered row has one folder; only the retained partial ports
        # (never registered, never menu rows) exist without one.
        self.assertEqual(set(folders) - {app_id.casefold() for app_id in entries},
                         {"ardulo", "evade", "montana", "slidingtiles", "waternet"})
        self.assertTrue({app_id.casefold() for app_id in entries} <= set(folders))
        normalized = {}
        for folder in sorted(path for path in GAMES.iterdir() if path.is_dir()):
            key = re.sub(r"[^a-z0-9]", "", folder.name.lower())
            self.assertNotIn(key, normalized,
                             f"near duplicate game folder: {folder.name} "
                             f"/ {normalized.get(key)}")
            normalized[key] = folder.name
        # No second "arduboy30" grouping folder duplicates the registry rows.
        grouping = [path for path in APPS.rglob("*")
                    if path.is_dir() and re.sub(r"[^a-z0-9]", "", path.name.lower())
                    == "arduboy30"]
        self.assertEqual(grouping, [])
        for app_id, entry in entries.items():
            header = read(folders[app_id.casefold()] / "app.h")
            self.assertRegex(header, r"\bclass\s+" + re.escape(entry.app_class) + r"\b",
                             app_id)

    def test_selection_header_carries_only_count_and_macros(self):
        chosen = self.build.arduboy30_selection(
            self.build.load_arduboy30_catalog(ROOT), ["pong", "quarto"])
        header = self.build.arduboy30_header(chosen)
        self.assertIn("#define CRABIT_ARDUBOY30_COUNT 2", header)
        self.assertIn("#define CRABIT_ARDUBOY30_ENABLE_PONG 1", header)
        self.assertIn("#define CRABIT_ARDUBOY30_ENABLE_QUARTO 1", header)
        for absent in ("Category::", "PongApp", '"Pong"', "app.h", "builtIn",
                       "ArduboyCategoryFolderApp"):
            self.assertNotIn(absent, header)
        # Selection stays explicit: the built-in six are never generated.
        for built_in in BUILT_IN:
            self.assertNotIn(self.build.arduboy30_macro(built_in), header)

    def test_category_menu_selectors_stay_exact_and_bounded(self):
        folder, math = self.build.arduboy30_link_extras([])
        self.assertEqual(folder, ("ArduboyCategoryFolderApp",))
        self.assertEqual(math, ())
        self.assertEqual(self.build.arduboy30_link_extras([{"id": "pong"}])[1],
                         self.build.ARDUBOY30_MATH_OBJECTS)
        self.assertEqual(self.build.arduboy30_support_selectors(),
                         self.build.ARDUBOY30_SUPPORT_SELECTORS)
        text = self.build.split_linker(
            "MEMORY\n{\n}\nSECTIONS\n{\n}\n",
            (*self.build.DEFAULT_SELECTORS, *folder, *self.build.arduboy30_support_selectors()))
        self.assertIn("ORIGIN = 0x08107000, LENGTH = 0xF1000", text)
        self.assertIn("*(.text.*ArduboyCategoryFolderApp* .rodata.*ArduboyCategoryFolderApp*)",
                      text)
        self.assertNotIn(".bss", text)
        self.assertNotIn(".data.*", text)
        for forbidden in ("libm.a:*(", "--whole-archive", "*sqrt*", ".extflash*"):
            self.assertNotIn(forbidden, text)


class RegistryCompileTests(unittest.TestCase):
    """Compile the real registry at its real include depth with stub Apps."""

    @classmethod
    def setUpClass(cls):
        cls.build = builder()
        cls.compiler = shutil.which("g++")
        if not cls.compiler:
            raise unittest.SkipTest("host g++ is required")
        cls._tmp = tempfile.TemporaryDirectory(prefix="crabit-registry27-")
        root = cls.root = Path(cls._tmp.name)
        (root / "app/core").mkdir(parents=True)
        shutil.copy2(REGISTRY, root / "app/arduboy30_registry.h")
        shutil.copy2(SKETCH / "app/core/app_factory.h", root / "app/core/app_factory.h")
        void_callback = re.search(r"typedef\s+void\s*\(\*VoidCallback\)\(\);", read(WIDGET))
        if not void_callback:
            raise AssertionError("real NanoUI VoidCallback signature changed")
        (root / "src").mkdir()
        (root / "src/NanoUI.h").write_text(
            "#ifndef NANOUI_STUB_H\n#define NANOUI_STUB_H\n"
            "#include <new>\n"
            + void_callback.group(0) + "\n"
            "class Application { public: virtual ~Application() {} };\n"
            "class AppManager {\n"
            "public:\n"
            "    static int launched;\n"
            "    static void launch(Application* app) { ++launched; delete app; }\n"
            "};\n"
            "namespace CrabitArduboy {\n"
            "template <typename T> T* tryCreate() { return new T(); }\n"
            "}\n#endif\n", encoding="utf-8")
        # Stubs mirror the real interface: the legacy apps (which include the
        # plain Hopper App) have no prepareForLaunch; managed ports do.
        managed = {app_id for app_id, _e in cls.build.load_arduboy30_registry_entries(ROOT)[1].items()
                   if app_id != "hopper"}
        _text, entries = cls.build.load_arduboy30_registry_entries(ROOT)
        for app_id, entry in entries.items():
            folder = root / "app/apps/games" / app_id
            folder.mkdir(parents=True)
            hook = ("    bool prepareForLaunch() { return true; }\n"
                    if app_id in managed else "")
            (folder / "app.h").write_text(
                f"#ifndef CRABIT_STUB_{app_id.upper()}_H\n"
                f"#define CRABIT_STUB_{app_id.upper()}_H\n"
                f"class {entry.app_class} : public Application {{\n"
                f"public:\n{hook}}};\n#endif\n", encoding="utf-8")
        (root / "main.cpp").write_text(
            "#include <cstdio>\n"
            "#include <type_traits>\n"
            '#include "app/arduboy30_registry.h"\n'
            "int AppManager::launched = 0;\n"
            "static_assert(std::is_same<decltype(Arduboy30Registry::Entry::launch),\n"
            "                           VoidCallback>::value,\n"
            '              "registry launch must stay a plain void(*)() function pointer");\n'
            "int main() {\n"
            "    size_t count = 0;\n"
            "    const Arduboy30Registry::Entry* rows = Arduboy30Registry::entries(count);\n"
            '    std::printf("count=%lu\\n", static_cast<unsigned long>(count));\n'
            "    for (size_t i = 0; i < count; ++i) {\n"
            '        std::printf("row=%s|%d|%s\\n", rows[i].id,\n'
            "                    static_cast<int>(rows[i].category), rows[i].label);\n"
            "        if (!rows[i].launch) return 2;\n"
            "    }\n"
            "    for (int c = 0;\n"
            "         c < static_cast<int>(Arduboy30Registry::Category::Count); ++c) {\n"
            "        const Arduboy30Registry::Category category =\n"
            "            static_cast<Arduboy30Registry::Category>(c);\n"
            '        std::printf("cat=%d|%lu|%s\\n", c,\n'
            "                    static_cast<unsigned long>(\n"
            "                        Arduboy30Registry::countIn(category)),\n"
            "                    Arduboy30Registry::categoryLabel(category));\n"
            "    }\n"
            "    for (size_t i = 0; i < count; ++i) rows[i].launch();\n"
            '    std::printf("launched=%d\\n", AppManager::launched);\n'
            "    return AppManager::launched == static_cast<int>(count) ? 0 : 3;\n"
            "}\n", encoding="utf-8")

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def run_selection(self, name, macros):
        executable = self.root / f"registry-{name}.exe"
        built = subprocess.run(
            [self.compiler, "-std=c++11", "-Wall", "-Wextra", "-I", str(self.root),
             *macros, str(self.root / "main.cpp"), "-o", str(executable)],
            text=True, capture_output=True, cwd=self.root)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(executable)], text=True, capture_output=True,
                             cwd=self.root)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        rows = [line.split("|") for line in ran.stdout.splitlines() if line.startswith("row=")]
        rows = [(row[0][len("row="):], CATEGORY_ORDER[int(row[1])], row[2]) for row in rows]
        counts = {}
        labels = {}
        for line in ran.stdout.splitlines():
            if not line.startswith("cat="):
                continue
            tag, count, label = line.split("|")
            index = int(tag[len("cat="):])
            counts[CATEGORY_ORDER[index]] = int(count)
            labels[CATEGORY_ORDER[index]] = label
        return rows, counts, labels, ran.stdout

    def test_default_image_lists_the_six_built_ins(self):
        rows, counts, labels, output = self.run_selection("default6", [])
        expected = [(app_id, category, label) for app_id, label, _c, category
                    in EXPECTED_ROWS if app_id in BUILT_IN]
        self.assertEqual(rows, expected)
        self.assertIn("count=6", output)
        self.assertEqual(counts, {"Shooter": 1, "JumpAction": 4, "Puzzle": 1,
                                  "Board": 0, "CasualRhythm": 0})
        self.assertEqual([labels[category] for category in CATEGORY_ORDER],
                         list(CATEGORY_LABELS))
        self.assertIn("launched=6", output)

    def test_all_27_categories_and_factories_compile_and_launch(self):
        macros = [f"-DCRABIT_ARDUBOY30_ENABLE_{self.build.arduboy30_macro(app_id)}=1"
                  for app_id, _l, _c, _cat in EXPECTED_ROWS]
        rows, counts, _labels, output = self.run_selection("all27", macros)
        self.assertEqual(rows, [(app_id, category, label) for app_id, label, _c, category
                                in EXPECTED_ROWS])
        self.assertIn("count=27", output)
        self.assertEqual(counts, EXPECTED_COUNTS)
        self.assertIn("launched=27", output)

    def test_reduced_selection_keeps_built_ins_and_hides_empty_categories(self):
        rows, counts, _labels, output = self.run_selection(
            "single", ["-DCRABIT_ARDUBOY30_ENABLE_PONG=1"])
        expected = [(app_id, category, label) for app_id, label, _c, category
                    in EXPECTED_ROWS if app_id in BUILT_IN or app_id == "pong"]
        self.assertEqual(rows, expected)
        self.assertIn("count=7", output)
        self.assertEqual(counts, {"Shooter": 1, "JumpAction": 4, "Puzzle": 1,
                                  "Board": 0, "CasualRhythm": 1})
        self.assertIn("launched=7", output)


class CategoryMenuTests(unittest.TestCase):
    def setUp(self):
        self.launcher = read(LAUNCHER)
        self.registry = read(REGISTRY)

    def test_launcher_uses_one_registry_driven_category_menu(self):
        text = self.launcher
        self.assertIn('#include "../arduboy30_registry.h"', text)
        self.assertIn('#include "../arduboy30_selected.h"', text)
        self.assertIn("class ArduboyCategoryFolderApp", text)
        self.assertIn("Arduboy30Registry::countIn(rows[i].category) == 0", text)
        self.assertIn("static_cast<size_t>(Arduboy30Registry::Category::Count)", text)
        self.assertEqual(text.count("ArduboyCategoryFolderApp<"), 5)
        # No old/new bucket and no second games list in the ports branch.
        self.assertNotIn("Arduboy 30", text)
        self.assertNotIn("Arduboy30FolderApp", text)
        self.assertEqual(text.count("buildArduboyCategoryMenu(titles, callbacks)"), 1)
        # The ports branch builds folders only; the reduced network/games
        # profiles keep their flat lists (Hopper routes through the managed
        # factory gate, not a direct construction).
        ports = text.split("class GamesFolderApp", 1)[1]
        ports = ports.split("#if CRABIT_BW16_ARDUBOY_PORTS", 1)[1]
        ports = ports.split("#elif CRABIT_BW16_NETWORK_GAME", 1)[0]
        for direct in ("new CastleBoyApp()", "new Arduboy3DApp()", "new AppHopper()",
                       "new HollowSeekerApp()", "new SameGameApp()", "new LasersApp()"):
            self.assertNotIn(direct, ports)

    def test_ports_profile_compiles_to_category_folders_only(self):
        result = preprocess(menu_source(), ["-DCRABIT_BW16_ARDUBOY_PORTS=1"])
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout
        for category in ("Shooter", "JumpAction", "Puzzle", "Board", "CasualRhythm"):
            self.assertRegex(
                output,
                r"new ArduboyCategoryFolderApp<\s*"
                r"Arduboy30Registry::Category::" + category + r">\(\)")
        self.assertIn("Arduboy30Registry::countIn(", output)
        self.assertNotIn('"Arduboy 30"', output)
        # Wireless/BLE and the games folder remain in the same image.
        self.assertIn("new GamesFolderApp()", output)
        self.assertIn("new WifiToolsMenuApp()", output)
        self.assertIn("new AppBleSpam()", output)

    def test_normal_profiles_never_compile_the_registry_menu(self):
        for profile in range(9):
            with self.subTest(profile=profile):
                result = preprocess(menu_source(), [f"-DCRABIT_BW16_PROFILE={profile}"])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("ArduboyCategoryFolderApp", result.stdout)
                self.assertNotIn("Arduboy30Registry::", result.stdout)

    def test_registry_preprocessing_matches_each_selection(self):
        result = preprocess(self.registry)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("builtInEntryFor<"), 6)
        self.assertEqual(result.stdout.count("entryFor<"), 0)
        selected = "".join(f"#define CRABIT_ARDUBOY30_ENABLE_"
                           f"{app_id.upper().replace('-', '_')} 1\n"
                           for app_id, _l, _c, _cat in EXPECTED_ROWS if app_id not in BUILT_IN)
        result = preprocess(selected + self.registry)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("builtInEntryFor<"), 6)
        self.assertEqual(result.stdout.count("entryFor<"), 21)
        # Same row shape and factory for both kinds.
        self.assertEqual(result.stdout.count("&launchOne<T>"), 2)
        self.assertNotIn("Arduboy30FolderApp", result.stdout)


if __name__ == "__main__":
    unittest.main()
