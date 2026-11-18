"""Catalogue and explicit-opt-in registry checks; no embedded build is run."""
import copy
import importlib.util
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
CATALOG = ROOT / "docs/nanoui/arduboy30-catalog.json"
REGISTRY = ROOT / "firmware/rtl8720dn/NanoUI/app/arduboy30_selected.h"
SKETCH = ROOT / "firmware/rtl8720dn/NanoUI"
FINAL_IDS = """ardubullets bananonsense chiemagari chribocchi evasion knightmove
ardugo minesweeper game2048 spacetanks galaxion stairssweep toyokumono rysk
reversi quarto asteroiddodge memoryrhythm pong jumpboy longcat""".split()


def builder():
    spec = importlib.util.spec_from_file_location("bw16_split", TOOLS / "build_bw16_split_games.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Arduboy30CatalogTests(unittest.TestCase):
    def setUp(self):
        self.build = builder()
        self.catalog = self.build.load_arduboy30_catalog(ROOT)
        self.games = self.catalog["games"]
        self.by_id = {game["id"]: game for game in self.games}

    def test_final_twenty_one_games_with_pinned_sources(self):
        self.assertEqual([game["id"] for game in self.games], FINAL_IDS)
        self.assertEqual(len(self.by_id), 21)
        self.assertEqual([game["index"] for game in self.games], list(range(21)))
        existing = set(self.catalog["existing_games_excluded"])
        self.assertFalse(existing & set(self.by_id))
        self.assertNotIn(self.catalog["template_excluded"], self.by_id)
        for game in self.games:
            source = game["source"]
            self.assertTrue(source["repository"].startswith("https://github.com/"))
            self.assertRegex(source["commit"], r"^[0-9a-f]{12,40}$")
            self.assertEqual(source["license"], "MIT")
            self.assertRegex(source["license_sha256"], r"^[0-9a-f]{64}$")
        self.assertTrue(self.by_id["ardubullets"]["status"].startswith("ported-"))
        for game in self.games:
            self.assertEqual(game["license_status"], "verified")
            self.assertIn(game["status"], {"ported-host-verified", "ported-embedded-verified"})

    def test_selection_is_explicit_and_verified_only(self):
        # Controlled fixture: do not couple this test to the changing ready set.
        fixture = copy.deepcopy(self.catalog)
        for game in fixture["games"]:
            game["status"] = "candidate"
            game["license_status"] = "verified"
        ready = [fixture["games"][0], fixture["games"][1], fixture["games"][3]]
        for game in ready:
            game["status"] = "ported-host-verified"
        expected = [game["id"] for game in ready]
        self.assertEqual([g["id"] for g in self.build.arduboy30_selection(
            fixture, expected)], expected)
        self.assertEqual([g["id"] for g in self.build.arduboy30_selection(
            fixture, ["all"])], expected)
        with self.assertRaisesRegex(ValueError, "not verified yet"):
            self.build.arduboy30_selection(fixture, [fixture["games"][2]["id"]])
        with self.assertRaises(ValueError):
            self.build.arduboy30_selection(fixture, ["not-a-game"])
        with self.assertRaisesRegex(ValueError, "cannot be combined"):
            self.build.arduboy30_selection(fixture, ["all", expected[0]])
        pending = copy.deepcopy(fixture)
        pending["games"][0]["license_status"] = "pending-asset-review"
        with self.assertRaisesRegex(ValueError, "licence is not verified"):
            self.build.arduboy30_selection(pending, [pending["games"][0]["id"]])
        # 'all' must apply the same field checks as explicit ids instead of
        # emitting a None namespace/class/label for an incomplete ready entry.
        for key in ("namespace", "app_class", "menu_label"):
            for requested in (["all"], [expected[0]]):
                incomplete = copy.deepcopy(fixture)
                incomplete["games"][0][key] = None
                with self.assertRaisesRegex(ValueError, f"lacks {key}"):
                    self.build.arduboy30_selection(incomplete, requested)
        invalid = copy.deepcopy(fixture)
        invalid["games"][0]["app_class"] = "Not A Class"
        for requested in (["all"], [expected[0]]):
            with self.assertRaisesRegex(ValueError, "not a C\\+\\+ identifier"):
                self.build.arduboy30_selection(invalid, requested)
        # A still-candidate entry may be incomplete; only selected entries matter.
        self.assertEqual([g["id"] for g in self.build.arduboy30_selection(
            fixture, ["all"])], expected)

    def test_production_all_matches_current_ready_set(self):
        ready = {g["id"] for g in self.games if g["status"] in
                 {"ported-host-verified", "ported-embedded-verified"}
                 and g["license_status"] == "verified"}
        chosen = {g["id"] for g in self.build.arduboy30_selection(self.catalog, ["all"])}
        self.assertEqual(chosen, ready)
        self.assertEqual(chosen, set(FINAL_IDS))
        self.assertEqual([g["id"] for g in self.build.arduboy30_selection(self.catalog, ["all"])],
                         FINAL_IDS)
        self.assertFalse(chosen & {"waternet", "montana", "evade", "ardulo", "slidingtiles"})
        self.assertEqual(len(self.build.arduboy30_paths(ROOT,
                         self.build.arduboy30_selection(self.catalog, ["all"]))), 21)

    def test_selected_license_hash_is_lf_normalized(self):
        # A CRLF checkout (ArduGo is one) must match the catalogue's LF digest.
        self.assertEqual(self.build.license_sha256(b"a\r\nb\r\n"),
                         self.build.license_sha256(b"a\nb\n"))
        self.assertNotEqual(self.build.license_sha256(b"a\r\nb"),
                            self.build.license_sha256(b"a\nb\n"))
        fixture = copy.deepcopy(self.catalog)
        game = next(g for g in fixture["games"] if g["id"] == "ardugo")
        game.update(status="ported-host-verified", license_status="verified",
                    namespace="ArduGoSandbox", app_class="ArduGoApp", menu_label="ArduGo")
        chosen = self.build.arduboy30_selection(fixture, ["ardugo"])
        self.assertEqual([g["id"] for g in chosen], ["ardugo"])
        self.assertEqual(self.build.arduboy30_paths(ROOT, chosen),
                         [SKETCH / "app/apps/games/ardugo/app.h"])
        bad = copy.deepcopy(game)
        bad["source"]["license_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "licence hash mismatch"):
            self.build.arduboy30_paths(ROOT, [bad])

    def test_generated_selection_and_default_are_separate(self):
        chosen = self.build.arduboy30_selection(self.catalog, ["ardubullets"])
        header = self.build.arduboy30_header(chosen)
        self.assertIn("#define CRABIT_ARDUBOY30_COUNT 1", header)
        self.assertIn("#define CRABIT_ARDUBOY30_ENABLE_ARDUBULLETS 1", header)
        # The generated file only selects ids; classes/labels stay in C++.
        self.assertNotIn("ArdubulletsApp", header)
        self.assertNotIn('"ARDUBULLET"', header)
        self.assertEqual(self.build.arduboy30_paths(ROOT, chosen),
                         [SKETCH / "app/apps/games/ardubullets/app.h"])
        default = REGISTRY.read_text(encoding="utf-8")
        self.assertIn("#define CRABIT_ARDUBOY30_COUNT 0", default)
        self.assertNotIn("CRABIT_ARDUBOY30_ENTRIES", default)
        empty = self.build.arduboy30_header([])
        self.assertIn("#define CRABIT_ARDUBOY30_COUNT 0", empty)
        self.assertNotIn("CRABIT_ARDUBOY30_ENABLE_", empty)

    def test_default_launcher_uses_the_one_categorized_registry(self):
        compiler = __import__("shutil").which("g++")
        if not compiler:
            self.skipTest("host g++ is required")
        launcher = (SKETCH / "app/core/launcher.h").read_text(encoding="utf-8")
        # The registry is always part of a ports image; it is not conditional
        # on a catalogue selection count.
        self.assertIn('#include "../arduboy30_registry.h"', launcher)
        self.assertNotIn("#if CRABIT_ARDUBOY30_COUNT", launcher)
        text = "\n".join(
            re.sub(r"^\s*#include[^\n]*", "", path.read_text(encoding="utf-8"), flags=re.M)
            for path in (SKETCH / "config.h", SKETCH / "src/CrabitBuildProfile.h",
                         SKETCH / "app/core/launcher.h"))
        result = subprocess.run(
            [compiler, "-E", "-P", "-x", "c++", "-DCRABIT_BW16_ARDUBOY_PORTS=1", "-"],
            input=text, text=True, capture_output=True, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout
        self.assertIn("buildArduboyCategoryMenu(titles, callbacks)", output)
        self.assertNotIn("Arduboy30FolderApp", output)
        self.assertNotIn('"Arduboy 30"', output)
        # No direct six-game list beside the registry; factories stay in C++.
        self.assertNotIn("new AppHopper()", output)
        self.assertNotIn("new CastleBoyApp()", output)
        self.assertNotIn("tryCreate<", output)

    def test_selected_menu_uses_the_factory_seam(self):
        compiler = __import__("shutil").which("g++")
        if not compiler:
            self.skipTest("host g++ is required")
        chosen = self.build.arduboy30_selection(self.catalog, ["ardubullets"])
        selected = self.build.arduboy30_header(chosen)
        body = "\n".join(
            re.sub(r"^\s*#include[^\n]*", "", path.read_text(encoding="utf-8"), flags=re.M)
            for path in (SKETCH / "config.h", SKETCH / "src/CrabitBuildProfile.h",
                         SKETCH / "app/arduboy30_registry.h",
                         SKETCH / "app/core/launcher.h"))
        result = subprocess.run(
            [compiler, "-E", "-P", "-x", "c++", "-DCRABIT_BW16_ARDUBOY_PORTS=1",
             "-DCRABIT_ARDUBOY30_ENABLE_ARDUBULLETS=1", "-"],
            input=selected + "\n" + body, text=True, capture_output=True, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('entryFor<ArdubulletsApp>("ardubullets", "ARDUBULLET", '
                      "Category::Shooter)", result.stdout)
        self.assertIn("CrabitArduboy::createForMenu", result.stdout)
        self.assertNotIn("new ArdubulletsApp()", result.stdout)
        self.assertIn("callbacks.push_back(entries[i].launch)", result.stdout)
        self.assertNotIn("[entries, i]", result.stdout)
        self.assertIn("if (app) AppManager::launch(app)", result.stdout)
        self.assertIn("Arduboy30Registry::categoryLabel", result.stdout)

    def test_registry_headers_compile_at_real_directory_depth(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("host g++ is required")
        _text, entries = self.build.load_arduboy30_registry(ROOT)
        with tempfile.TemporaryDirectory(prefix="crabit-registry-") as directory:
            root = Path(directory)
            (root / "app/core").mkdir(parents=True)
            shutil.copy2(SKETCH / "app/arduboy30_registry.h",
                         root / "app/arduboy30_registry.h")
            shutil.copy2(SKETCH / "app/core/app_factory.h",
                         root / "app/core/app_factory.h")
            (root / "src").mkdir()
            widget = (ROOT / "libraries/NanoUI/src/NanoUI/product/framework/widget.h"
                      ).read_text(encoding="utf-8")
            void_callback = re.search(r"typedef\s+void\s*\(\*VoidCallback\)\(\);", widget)
            self.assertIsNotNone(void_callback, "real NanoUI VoidCallback signature changed")
            # Isolated stubs keep the real app/, app/core/ and app/apps/games/
            # depth so a wrong registry include path fails here, without touching
            # the real game fixtures or headers. The callback typedef is copied
            # from the real NanoUI header, so std::function cannot mask a mismatch.
            (root / "src/NanoUI.h").write_text(
                "#ifndef NANOUI_STUB_H\n#define NANOUI_STUB_H\n"
                "#include <type_traits>\n"
                "class Application { public: virtual ~Application() {} };\n"
                + void_callback.group(0) + "\n"
                "static_assert(std::is_same<VoidCallback, void (*)()>::value,\n"
                "              \"menu callbacks must be plain function pointers\");\n"
                "class AppManager { public: static void launch(Application* app) { delete app; } };\n"
                "namespace CrabitArduboy {\n"
                "template <typename T> T* tryCreate() { return new T(); }\n"
                "}\n#endif\n", encoding="utf-8")
            for app_id, (_label, app_class) in entries.items():
                folder = root / "app/apps/games" / app_id
                folder.mkdir(parents=True)
                (folder / "app.h").write_text(
                    f"#ifndef CRABIT_STUB_{app_id.upper()}_H\n"
                    f"#define CRABIT_STUB_{app_id.upper()}_H\n"
                    f"class {app_class} : public Application {{\n"
                    "public:\n    bool prepareForLaunch() { return true; }\n};\n#endif\n",
                    encoding="utf-8")
            (root / "main.cpp").write_text(
                '#include <vector>\n'
                '#include "app/arduboy30_registry.h"\n'
                'class PlainMenuApp : public Application {};\n'
                'int main() {\n'
                '    size_t count = 0;\n'
                '    const Arduboy30Registry::Entry* entries =\n'
                '        Arduboy30Registry::entries(count);\n'
                '    std::vector<VoidCallback> callbacks;\n'
                '    for (size_t i = 0; i < count; i++)\n'
                '        callbacks.push_back(entries[i].launch);\n'
                '    const Arduboy30Registry::Entry plainEntry =\n'
                '        Arduboy30Registry::entryFor<PlainMenuApp>(\n'
                '            "plain", "Plain", Arduboy30Registry::Category::Puzzle);\n'
                '    callbacks.push_back(plainEntry.launch);\n'
                '    Application* plain = CrabitArduboy::createForMenu<PlainMenuApp>();\n'
                '#ifdef CRABIT_ARDUBOY30_ENABLE_PONG\n'
                '    Application* port = CrabitArduboy::createForMenu<PongApp>();\n'
                '#else\n'
                '    Application* port = CrabitArduboy::createForMenu<PlainMenuApp>();\n'
                '#endif\n'
                '    const bool ok = plain && port && count && plainEntry.launch;\n'
                '    delete plain;\n    delete port;\n'
                '    if (!callbacks.empty()) callbacks[0]();\n'
                '    plainEntry.launch();\n'
                '    return ok ? 0 : 1;\n}\n', encoding="utf-8")
            single = [f"-DCRABIT_ARDUBOY30_ENABLE_{self.build.arduboy30_macro('pong')}=1"]
            all_ids = [f"-DCRABIT_ARDUBOY30_ENABLE_"
                       f"{self.build.arduboy30_macro(app_id)}=1" for app_id in entries]
            for name, macros in (("default", []), ("single", single), ("all27", all_ids)):
                executable = root / f"registry-{name}.exe"
                build = subprocess.run(
                    [compiler, "-std=c++11", "-I", str(root), *macros,
                     str(root / "main.cpp"), "-o", str(executable)],
                    text=True, capture_output=True, cwd=root)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                run = subprocess.run([str(executable)], text=True, capture_output=True, cwd=root)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_cpp_registry_matches_catalogue(self):
        text, entries = self.build.load_arduboy30_registry_entries(ROOT)
        self.assertEqual(len(entries), 27)
        self.assertEqual({app_id for app_id, entry in entries.items() if not entry.built_in},
                         set(FINAL_IDS))
        self.assertEqual({app_id for app_id, entry in entries.items() if entry.built_in},
                         set(self.catalog["existing_games_excluded"]))
        self.build.check_registry_catalog(self.catalog, entries)
        for game in self.games:
            with self.subTest(game=game["id"]):
                entry = entries[game["id"]]
                self.assertEqual(entry.label, game["menu_label"])
                self.assertEqual(entry.app_class, game["app_class"])
                self.assertIn(entry.category, {"Shooter", "JumpAction", "Puzzle",
                                               "Board", "CasualRhythm"})
                package = SKETCH / "app/apps/games" / game["id"]
                self.assertFalse((package / "app.json").exists())
                self.assertTrue((package / "app.h").is_file())
                self.assertTrue((package / "README.md").is_file())
                self.assertTrue((package / "LICENSE").is_file())
        self.assertIn("};", text)


if __name__ == "__main__":
    unittest.main()
