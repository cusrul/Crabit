"""Preprocessor/scope controls for the temporary BW16 App-size experiment.

These do not validate GUI pixels, radio timing, or runtime free heap.
"""

from pathlib import Path
import re
import shutil
import subprocess
import unittest
import os
import tempfile
from tools.tests.test_bw16_uart_ingress import extract_function

ROOT = Path(__file__).resolve().parents[2]
BASE = "164b4f4f24603ee68ac8770a0f48a41af2a19e82"
# The experiment's scope is historical. Later authorized radio/docs work must
# not be mistaken for part of the original two-file footprint-only change.
FOOTPRINT_COMMIT = "549f3a3ab0b353a2f13994b35384184a0ad04ec0"
MENU_BASE = "ce7a85c"  # User-accepted Chinese menu/placeholder cleanup.
LAUNCHER = "firmware/rtl8720dn/NanoUI/app/launcher.h"
CONFIG = "firmware/rtl8720dn/NanoUI/config.h"


def without_includes(source):
    return re.sub(r"^\s*#include[^\n]*", "", source, flags=re.MULTILINE)


class SmallAppFootprintTests(unittest.TestCase):
    def preprocess(self, launcher, enabled=None, profile=0, two_games=False, game=0, network=False):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "existing host g++ is required")
        # Actual conditional code, without importing embedded SDK headers.
        source = "\n".join(
            without_includes(text)
            for text in (
                (ROOT / CONFIG).read_text(encoding="utf-8"),
                (ROOT / "firmware/rtl8720dn/NanoUI/src/CrabitBuildProfile.h").read_text(
                    encoding="utf-8"
                ),
                launcher,
            )
        )
        command = [compiler, "-E", "-P", "-x", "c++", f"-DCRABIT_BW16_PROFILE={profile}"]
        command.append(f"-DCRABIT_BW16_ENABLE_NETWORK_SYNC={int(network)}")
        if two_games:
            command.append("-DCRABIT_BW16_NETWORK_TWO_GAMES=1")
        if game:
            command.append(f"-DCRABIT_BW16_NETWORK_GAME={game}")
        if enabled is not None:
            command.append(f"-DCRABIT_BW16_DIAG_SMALL_APP_SET={enabled}")
        result = subprocess.run(
            command + ["-"], input=source, text=True, capture_output=True, cwd=ROOT
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_ide_default_keeps_debug_source_but_hides_test_entries(self):
        launcher = (ROOT / LAUNCHER).read_text(encoding="utf-8")
        selected = self.preprocess(launcher)
        for present in (
            "class PairDiagApp",
            '"配对诊断"',
            '"CRABIT"',
            '"请使用主控设置"',
            "new ModernMenu",
            "class GamesFolderApp",
            "class SystemFolderApp",
        ):
            self.assertIn(present, selected)
        for hidden in ("new PairDiagApp()", "new ProductNoticeApp()"):
            self.assertNotIn(hidden, selected)
        self.assertNotIn("class ProductNoticeApp", selected)
        for absent in (
            "asset_my_media",
            "new AnimatedImage",
            "new AppBleMouse()",
            "new CastleBoyApp()",
            "new WifiProbeApp()",
            "current_mode = MODE_OTA",
        ):
            self.assertNotIn(absent, selected)
        self.assertIn("return new LauncherApp();", selected)
        self.assertEqual(selected, self.preprocess(launcher, enabled=1))

    def test_switch_off_and_lab_profiles_recover_original_launcher(self):
        launcher = (ROOT / LAUNCHER).read_text(encoding="utf-8")
        self.assertIn("std::vector<const char*> titles", launcher)
        self.assertNotIn("titles[i].c_str()", launcher)
        baseline = subprocess.check_output(
            ["git", "show", f"{MENU_BASE}:{LAUNCHER}"], text=True, encoding="utf-8", cwd=ROOT
        )

        # SystemFolderApp now honors ENABLE_OTA; its feature behavior is tested
        # separately. All other historical launcher content must still match.
        def normalize(source, profile):
            # The later verified menu fix borrows literal pointers instead of
            # dangling c_str() pointers from a destroyed vector<string>.
            source = source.replace(
                "std::vector<std::string> titles", "std::vector<const char*> titles"
            )
            source = source.replace("std::vector<std::string>{", "std::vector<const char*>{")
            # The placeholder notice adopted the same explicitly chosen 8px UI;
            # do not normalize arbitrary widget sizes or actual profile choices.
            source = source.replace('new Text("暂无应用", 12)', 'new Text("暂无应用", 8)')
            source = source.replace('new Text("返回", 12)', 'new Text("返回", 8)')
            helper = extract_function(source, "inline Widget* buildModernAnimMenu(")
            source = source.replace(
                helper, "/* factory covered by current menu/render/lifetime tests */", 1
            )
            source = re.sub(
                r"class SystemFolderApp.*?(?=class LauncherApp)", "", source, flags=re.S
            )
            if profile == 5:  # Network-only intentionally excludes PAIR DIAG.
                source = re.sub(
                    r"class BleFolderApp.*?(?=class GamesFolderApp)", "", source, flags=re.S
                )
            return " ".join(source.split())

        for profile in range(7):
            with self.subTest(profile=profile):
                original = self.preprocess(baseline, enabled=0, profile=profile)
                self.assertEqual(
                    normalize(original, profile),
                    normalize(self.preprocess(launcher, enabled=0, profile=profile), profile),
                )
                if profile != 0:
                    self.assertEqual(
                        normalize(original, profile),
                        normalize(self.preprocess(launcher, enabled=1, profile=profile), profile),
                    )

    def test_actual_product_menu_and_folder_back(self):
        selected = self.preprocess((ROOT / LAUNCHER).read_text(encoding="utf-8"), network=True)
        # Compile the actual selected menu, not SDK-dependent diagnostic classes.
        menu = selected[selected.index("static const uint8_t icon_hacker_main") :]
        existing = (ROOT / "tools/tests/stm32_settings_flat.cpp").read_text(encoding="utf-8")
        fixture = (
            r"""
#include "NanoUI.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace NanoUI;
"""
            + (ROOT / "firmware/rtl8720dn/NanoUI/src/network_ui/strings.h").read_text(
                encoding="utf-8"
            )
            + extract_function(existing, "class TestHeap final : public NanoUI::MemoryAdapter")
            + ";\n"
            + extract_function(existing, "class BitmapCanvas final : public Canvas")
            + ";\n"
            + menu
            + r"""
uint32_t ticks=0;
uint32_t getTicks(){return ticks;}
void settle(WindowManager& wm){for(int i=0;i<100;++i){ticks+=16;wm.drawFrame();}}
void send(WindowManager& wm,EventType type){Event e{};e.type=type;wm.dispatchEvent(e);settle(wm);}
int main(int argc,char** argv){
 TestHeap heap;MemoryPool::instance().setAdapter(&heap);HAL_Time::init(getTicks,nullptr);
 BitmapCanvas canvas;WindowManager wm(&canvas,nullptr);AppManager::init(&canvas,nullptr);
 class Anchor:public Application{public:const char* getAppName()override{return "ANCHOR";} Widget* buildUI()override{return new SizedBox(1,1);}};
 AppManager::launch(new Anchor());AppManager::launch(new LauncherApp());settle(wm);
 assert(wm.getStackSize()==2);
 if(argc>1)assert(canvas.saveBmp(std::string(argv[1])+"/launcher.bmp"));
 send(wm,EVT_OK);assert(wm.getStackSize()==1); // Trial page's actual BACK button.
 AppManager::shutdown();
}
"""
        )
        ui = ROOT / "firmware/stm32/Core/NanoUI"
        shared = ROOT / "libraries/NanoUI"
        with tempfile.TemporaryDirectory(prefix="crabit-product-menu-") as folder:
            cpp = Path(folder) / "menu.cpp"
            exe = Path(folder) / "menu.exe"
            cpp.write_text(fixture, encoding="utf-8")
            # This fixture explicitly renders the retained standalone-network
            # trial and links its compact font, independent of IDE defaults.
            command = [
                shutil.which("g++"),
                "-std=c++17",
                "-O0",
                "-DCRABIT_BW16_ENABLE_NETWORK_SYNC=1",
                "-I",
                str(ui / "include"),
                "-I",
                str(ui / "assets/generated/desktop"),
                "-I",
                str(shared / "src"),
                str(cpp),
                str(ROOT / "firmware/rtl8720dn/NanoUI/src/font_engine.cpp"),
                str(shared / "src/NanoUI/product/runtime.cpp"),
                str(ROOT / "firmware/rtl8720dn/NanoUI/src/network_font_data.cpp"),
                "-o",
                str(exe),
            ]
            built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            frames = os.environ.get("CRABIT_PRODUCT_MENU_FRAMES")
            if frames:
                Path(frames).mkdir(parents=True, exist_ok=True)
            ran = subprocess.run(
                [str(exe)] + ([frames] if frames else []), capture_output=True, text=True
            )
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_all_lab_registers_all_existing_payload_groups(self):
        selected = self.preprocess((ROOT / LAUNCHER).read_text(encoding="utf-8"), profile=7)
        for app in (
            "AppBleMouse",
            "AppBlePPT",
            "AppBleKbdTest",
            "CastleBoyApp",
            "Arduboy3DApp",
            "AppHopper",
            "WifiToolsMenuApp",
        ):
            self.assertIn(f"new {app}()", selected)
        self.assertIn("current_mode = MODE_OTA", selected)
        self.assertNotIn("new PairDiagApp()", selected)
        self.assertNotIn("new ProductNoticeApp()", selected)

    def test_network_ota_excludes_hid_and_games(self):
        selected = self.preprocess((ROOT / LAUNCHER).read_text(encoding="utf-8"), profile=8)
        for app in (
            "AppBleMouse",
            "AppBlePPT",
            "AppBleKbdTest",
            "CastleBoyApp",
            "Arduboy3DApp",
            "AppHopper",
        ):
            self.assertNotIn(f"new {app}()", selected)
        self.assertIn("new WifiToolsMenuApp()", selected)
        self.assertIn("current_mode = MODE_OTA", selected)

    def test_network_only_excludes_ota_hid_and_games(self):
        selected = self.preprocess((ROOT / LAUNCHER).read_text(encoding="utf-8"), profile=5)
        for app in (
            "AppBleMouse",
            "AppBlePPT",
            "AppBleKbdTest",
            "CastleBoyApp",
            "Arduboy3DApp",
            "AppHopper",
        ):
            self.assertNotIn(f"new {app}()", selected)
        self.assertIn("new WifiToolsMenuApp()", selected)
        self.assertNotIn("current_mode = MODE_OTA", selected)
        self.assertNotIn("new PairDiagApp()", selected)

    def test_two_game_network_trial_keeps_only_selected_games(self):
        selected = self.preprocess(
            (ROOT / LAUNCHER).read_text(encoding="utf-8"), profile=5, two_games=True
        )
        for app in ("Arduboy3DApp", "AppHopper", "WifiToolsMenuApp"):
            self.assertIn(f"new {app}()", selected)
        for app in ("CastleBoyApp", "AppBleMouse", "AppBlePPT", "AppBleKbdTest", "PairDiagApp"):
            self.assertNotIn(f"new {app}()", selected)
        self.assertNotIn("current_mode = MODE_OTA", selected)

    def test_single_game_network_selection(self):
        for game, included, excluded in (
            (1, "Arduboy3DApp", "AppHopper"),
            (2, "AppHopper", "Arduboy3DApp"),
        ):
            selected = self.preprocess(
                (ROOT / LAUNCHER).read_text(encoding="utf-8"), profile=5, game=game
            )
            self.assertIn(f"new {included}()", selected)
            self.assertNotIn(f"new {excluded}()", selected)
            self.assertNotIn("new CastleBoyApp()", selected)
            self.assertIn("new WifiToolsMenuApp()", selected)

    def test_compact_terminate_keeps_allocation_exceptions_and_abort(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        source = ROOT / "firmware/rtl8720dn/NanoUI/src/compact_terminate.cpp"
        with tempfile.TemporaryDirectory() as temporary:
            exe = Path(temporary) / "terminate-probe.exe"
            # Replace abort in the host fixture only, avoiding a Windows crash
            # dialog while proving the actual handler calls the fatal path.
            probe = """#include <cstdlib>
#include <exception>
extern "C" void abort() noexcept { std::_Exit(73); }
int main(int argc, char**) {
    int* value = new int(37);
    bool ok = *value == 37;
    delete value;
    try { throw 42; } catch (int caught) { ok = ok && caught == 42; }
    if (argc > 1) std::terminate();
    return ok ? 0 : 1;
}
"""
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-DCRABIT_BW16_PROFILE=5",
                    str(source),
                    "-x",
                    "c++",
                    "-",
                    "-o",
                    str(exe),
                ],
                input=probe,
                capture_output=True,
                text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)
            self.assertEqual(subprocess.run([str(exe), "fatal"]).returncode, 73)

    def test_original_footprint_commit_left_protected_paths_unchanged(self):
        protected = [
            "firmware/stm32",
            "firmware/shared",
            "libraries/NanoUI",
            "apps/mobile",
            "firmware/rtl8720dn/NanoUI/RadioSvc.cpp",
            "firmware/rtl8720dn/NanoUI/RadioSvc.h",
            "firmware/rtl8720dn/NanoUI/NetManager.cpp",
            "firmware/rtl8720dn/NanoUI/src/main_entry.cpp",
            "firmware/rtl8720dn/NanoUI/src/nanoui_assets.h",
            "firmware/rtl8720dn/NanoUI/bw16_oled.h",
            "tools/build-rtl8720dn.ps1",
            "tools/check-bw16-image.ps1",
        ]
        changed = subprocess.check_output(
            ["git", "diff", "--name-only", BASE, FOOTPRINT_COMMIT, "--", *protected],
            text=True,
            cwd=ROOT,
        )
        self.assertEqual(changed.strip(), "")


if __name__ == "__main__":
    unittest.main()
