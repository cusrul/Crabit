"""Hopper's managed factory route in the reduced BW16 profiles. Host-only.

Hopper is now an ArduboyCompatApp (port id 21). The games-lab and IDE-trial
menus must not construct it directly: prepareForLaunch() has to run first, and
the launch is skipped when resource preparation fails. The builder must also
select HopperSandbox exactly, because that namespace owns the port's code and
readonly assets; AppHopper stays the class selector.
"""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_bw16_uart_ingress import extract_function

ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / "firmware/rtl8720dn/NanoUI"
LAUNCHER = SKETCH / "app/core/launcher.h"
BUILDER = ROOT / "tools/build_bw16_split_games.py"
SCRIPT = "MEMORY\n{\n}\nSECTIONS\n{\n}\n"


def builder():
    spec = importlib.util.spec_from_file_location("bw16_split_hopper", BUILDER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def preprocess(flags=()):
    """Expand the real launcher conditionals without embedded includes."""
    compiler = shutil.which("g++")
    if not compiler:
        raise unittest.SkipTest("host g++ is required")
    text = "\n".join(
        re.sub(r"^\s*#include[^\n]*", "", path.read_text(encoding="utf-8"), flags=re.M)
        for path in (SKETCH / "config.h", SKETCH / "src/CrabitBuildProfile.h", LAUNCHER)
    )
    return subprocess.run([compiler, "-E", "-P", "-x", "c++", *flags, "-"],
                          input=text, text=True, capture_output=True, cwd=ROOT)


class LauncherHopperRouteTests(unittest.TestCase):
    def setUp(self):
        self.text = LAUNCHER.read_text(encoding="utf-8")

    def test_no_direct_hopper_construction_remains(self):
        self.assertNotIn("new AppHopper()", self.text)
        self.assertEqual(self.text.count("launchPreparedArduboyApp<AppHopper>"), 2)

    def test_factory_include_and_helper_are_guarded(self):
        guard = ('#if CRABIT_BW16_ENABLE_GAMES || CRABIT_BW16_NETWORK_GAME || '
                 'CRABIT_BW16_ARDUBOY_PORTS')
        include = self.text.index('#include "app_factory.h"')
        self.assertGreater(include, self.text.index(guard))
        self.assertLess(include, self.text.index('#if CRABIT_BW16_ARDUBOY_PORTS\n'
                                                 '#include "../apps/games/hollow/app.h"'))
        self.assertIn('#if !CRABIT_BW16_ARDUBOY_PORTS && '
                      '(CRABIT_BW16_ENABLE_GAMES || CRABIT_BW16_NETWORK_GAME)',
                      self.text)
        self.assertIn("Application* app = CrabitArduboy::createForMenu<T>();",
                      self.text)
        self.assertIn("if (app) AppManager::launch(app);", self.text)

    def test_reduced_profiles_use_the_factory_gate(self):
        for name, flags in (
            ("games-lab", ["-DCRABIT_BW16_PROFILE=4"]),
            ("all-lab", ["-DCRABIT_BW16_PROFILE=7"]),
            ("network-hopper", ["-DCRABIT_BW16_PROFILE=5",
                                "-DCRABIT_BW16_NETWORK_GAME=2"]),
            # IDE default: wifi-tools profile plus the Hopper trial macro.
            ("ide-trial", []),
        ):
            with self.subTest(profile=name):
                result = preprocess(flags)
                self.assertEqual(result.returncode, 0, result.stderr)
                output = result.stdout
                self.assertIn("launchPreparedArduboyApp<AppHopper>", output)
                self.assertNotIn("new AppHopper()", output)
                # The gate still declines to launch when preparation fails.
                self.assertIn("if (app) AppManager::launch(app);", output)

    def test_profiles_without_hopper_do_not_compile_the_route(self):
        for name, flags in (
            ("ota-lab", ["-DCRABIT_BW16_PROFILE=6"]),
            ("wifi-only", ["-DCRABIT_BW16_PROFILE=5"]),
        ):
            with self.subTest(profile=name):
                result = preprocess(flags)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("launchPreparedArduboyApp<AppHopper>", result.stdout)
                self.assertNotIn("new AppHopper()", result.stdout)

    def test_ports_image_gets_the_registry_route_only(self):
        result = preprocess(["-DCRABIT_BW16_ARDUBOY_PORTS=1"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Arduboy30Registry::entries(", result.stdout)
        self.assertNotIn("launchPreparedArduboyApp<", result.stdout)
        self.assertNotIn("new AppHopper()", result.stdout)

    def test_factory_gate_launches_only_after_successful_preparation(self):
        compiler = shutil.which("g++")
        if not compiler:
            raise unittest.SkipTest("host g++ is required")
        helper = extract_function(self.text, "inline void launchPreparedArduboyApp()")
        fixture = (
            r"""
struct Application { virtual ~Application() {} };
namespace AppManager {
inline int launches = 0;
inline Application* last = nullptr;
inline void launch(Application* app) { last = app; ++launches; }
}  // namespace AppManager
namespace CrabitArduboy {
inline bool allow = true;
inline int created = 0;
template <typename T>
Application* createForMenu() {
    ++created;
    return allow ? new T() : nullptr;
}
}  // namespace CrabitArduboy
template <typename T>
"""
            + helper
            + r"""
int main() {
    launchPreparedArduboyApp<Application>();
    if (CrabitArduboy::created != 1 || AppManager::launches != 1) return 1;
    CrabitArduboy::allow = false;
    launchPreparedArduboyApp<Application>();
    // Preparation ran again but nothing was mounted.
    if (CrabitArduboy::created != 2 || AppManager::launches != 1) return 2;
    CrabitArduboy::allow = true;
    launchPreparedArduboyApp<Application>();
    if (CrabitArduboy::created != 3 || AppManager::launches != 2) return 3;
    return 0;
}
"""
        )
        with tempfile.TemporaryDirectory(prefix="crabit-hopper-gate-") as folder:
            source = Path(folder) / "gate.cpp"
            exe = Path(folder) / "gate.exe"
            source.write_text(fixture, encoding="utf-8")
            built = subprocess.run([compiler, "-std=c++17", str(source), "-o", str(exe)],
                                   capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


class BuilderHopperSelectionTests(unittest.TestCase):
    def setUp(self):
        self.build = builder()

    def test_default_selectors_keep_classes_exact(self):
        build = self.build
        self.assertEqual(build.selectors([]), build.DEFAULT_SELECTORS)
        self.assertIn("AppHopper", build.DEFAULT_SELECTORS)
        self.assertIn("HopperSandbox", build.DEFAULT_SELECTORS)
        self.assertIn("HopperSandbox", build.GAME_NAMESPACES)
        self.assertNotIn("AppHopper", build.GAME_NAMESPACES)
        # No bare Hopper wildcard: both names must stay exact.
        self.assertNotIn("Hopper", build.DEFAULT_SELECTORS)
        text = build.split_linker(SCRIPT, build.DEFAULT_SELECTORS)
        for token in ("AppHopper", "HopperSandbox"):
            self.assertIn(f"*(.text.*{token}* .rodata.*{token}*)", text)
        self.assertNotIn("*(.text.*Hopper* .rodata.*Hopper*)", text)

    def test_hopper_sandbox_needs_code_and_readonly_assets_in_extension(self):
        build = self.build
        code = "08107000 00000010 t HopperSandbox::setup()\n"
        assets = "08107010 00000010 r HopperSandbox::imgPlayers\n"
        build.check_symbols(code + assets, ("HopperSandbox",), 0x100,
                            asset_names=("HopperSandbox",))
        # A code/rodata split is not required: Hopper's PROGMEM is empty, so a
        # constant may be folded into the code section but must still be an
        # ELF object inside .extflash.
        objects = "08107010 l     O .extflash\t00000010 HopperSandbox::imgPlayers\n"
        build.check_symbols(code + "08107010 00000010 t HopperSandbox::imgPlayers\n",
                            ("HopperSandbox",), 0x100,
                            asset_names=("HopperSandbox",), objects=objects)
        for text in (code, assets, code + assets.replace("08107010", "0e007010")):
            with self.subTest(text=text.splitlines()[-1]):
                with self.assertRaisesRegex(ValueError, "needs both real code and readonly"):
                    build.check_symbols(text, ("HopperSandbox",), 0x100,
                                        asset_names=("HopperSandbox",))

    def test_default_image_still_contains_the_full_hopper(self):
        build = self.build
        _text, entries = build.load_arduboy30_registry_entries(ROOT)
        self.assertTrue(entries["hopper"].built_in)
        self.assertEqual(entries["hopper"].app_class, "AppHopper")
        menu = build.arduboy30_menu(entries, [])
        enabled = [app_id for ids in menu.values() for app_id in ids]
        self.assertEqual(sorted(enabled), sorted(build.ARDUBOY30_BUILT_IN))
        self.assertIn("hopper", menu["JumpAction"])


if __name__ == "__main__":
    unittest.main()
