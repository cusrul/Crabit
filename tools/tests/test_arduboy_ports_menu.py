"""Compile actual menu/profile conditionals without the embedded SDK."""
from pathlib import Path
import hashlib
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / "firmware/rtl8720dn/NanoUI"
APPS = ("HollowSeekerApp", "SameGameApp", "LasersApp")


class ArduboyPortsMenuTests(unittest.TestCase):
    def preprocess(self, flags=()):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "host g++ is required")
        text = "\n".join(
            re.sub(r"^\s*#include[^\n]*", "", p.read_text(encoding="utf-8"), flags=re.M)
            for p in (SKETCH / "config.h", SKETCH / "src/CrabitBuildProfile.h",
                      SKETCH / "app/core/launcher.h")
        )
        return subprocess.run([compiler, "-E", "-P", "-x", "c++", *flags, "-"],
                              input=text, text=True, capture_output=True, cwd=ROOT)

    def test_normal_profiles_do_not_register_new_games(self):
        for profile in (None, *range(9)):
            with self.subTest(profile=profile):
                flags = [] if profile is None else [f"-DCRABIT_BW16_PROFILE={profile}"]
                result = self.preprocess(flags)
                self.assertEqual(result.returncode, 0, result.stderr)
                for app in APPS:
                    self.assertNotIn(f"new {app}()", result.stdout)
                if profile is None:
                    # Hopper is a managed ArduboyCompatApp port, so the IDE
                    # trial builds the factory gate instead of a direct new.
                    self.assertIn("launchPreparedArduboyApp<AppHopper>", result.stdout)
                    self.assertNotIn("new AppHopper()", result.stdout)
                    self.assertNotIn("new CastleBoyApp()", result.stdout)

    def test_opt_in_builds_category_folders_and_retains_wifi_ble(self):
        # Deliberately keep IDE's NETWORK_GAME=2: opt-in menu must take precedence.
        for extra in ([], ["-DCRABIT_BW16_NETWORK_GAME=0"]):
            result = self.preprocess(["-DCRABIT_BW16_ARDUBOY_PORTS=1", *extra])
            self.assertEqual(result.returncode, 0, result.stderr)
            output = result.stdout
            for app in ("GamesFolderApp", "AppBleSpam", "WifiToolsMenuApp"):
                self.assertIn(f"new {app}()", output)
            self.assertIn("buildArduboyCategoryMenu", output)
            self.assertEqual(output.count("ArduboyCategoryFolderApp<"), 5)
            # The registry owns every game row now, including the built-in six,
            # so the launcher no longer constructs them directly.
            for app in ("CastleBoyApp", "Arduboy3DApp", "AppHopper", *APPS):
                self.assertNotIn(f"new {app}()", output)
            self.assertNotIn('"Arduboy 30"', output)
            self.assertNotIn("current_mode = MODE_OTA", output)

    def test_opt_in_rejects_wrong_profile_and_non_boolean(self):
        for flags in (["-DCRABIT_BW16_ARDUBOY_PORTS=1", "-DCRABIT_BW16_PROFILE=0"],
                      ["-DCRABIT_BW16_ARDUBOY_PORTS=2"]):
            result = self.preprocess(flags)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Arduboy ports", result.stderr)

    def test_upstream_implementation_is_not_auto_compiled(self):
        # Arduino recursively compiles sketch src/, but manually included game
        # bodies should also remain unambiguous to other source-discovery tools.
        pin = "d4b1f041789dcd1d71907654e4025d613b4ab420"
        expected = "62a5db2ff173434f94d055a9432f5655267e92f4db92a042b832884e8a48f6bb"
        for game in ("hollow", "samegame", "lasers"):
            folder = SKETCH / "app/apps/games" / game
            self.assertFalse(list(folder.rglob("*.cpp")))
            self.assertFalse(list(folder.rglob("*.ino")))
            self.assertFalse(list(folder.rglob("*.inc")), "Arduino staging omits .inc; use .inc.h")
            license_text = (folder / "LICENSE").read_text(encoding="utf-8")
            self.assertEqual(hashlib.sha256(license_text.encode("utf-8")).hexdigest(), expected)
            self.assertIn(pin, (folder / "README.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
