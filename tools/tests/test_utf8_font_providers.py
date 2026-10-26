"""Exercise each real platform font provider against the shared decoder contract."""

from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "libraries/NanoUI"


class FontProviderUtf8Tests(unittest.TestCase):
    def test_both_platform_providers_use_safe_shared_decoder(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "Existing host g++ required; no SDK or installation used")
        stm32 = ROOT / "firmware/stm32/Core/NanoUI"
        bw16 = ROOT / "firmware/rtl8720dn/NanoUI"
        first_app = LIBRARY / "examples/FirstApp/src/generated"
        providers = {
            "stm32": (
                stm32 / "include",
                stm32 / "src/font_engine.cpp",
                stm32 / "src/nanoui_assets.cpp",
            ),
            "bw16": (bw16 / "src", bw16 / "src/font_engine.cpp", bw16 / "src/nanoui_assets.cpp"),
            "first-app": (
                first_app,
                first_app / "nanoui_font_provider.cpp",
                first_app / "nanoui_assets.cpp",
            ),
        }
        for platform, (includes, provider, assets) in providers.items():
            with self.subTest(platform=platform):
                output = ROOT / "build/utf8-provider-tests" / platform
                output.mkdir(parents=True, exist_ok=True)
                binary = output / "utf8-provider.exe"
                command = [
                    compiler,
                    "-std=c++11",
                    "-O2",
                    "-DNANOUI_TEST_FONT_PROVIDER=1",
                    "-I",
                    str(LIBRARY / "src"),
                    "-I",
                    str(includes),
                    str(LIBRARY / "tests/utf8_tests.cpp"),
                    str(provider),
                    str(assets),
                    "-o",
                    str(binary),
                ]
                build = subprocess.run(
                    command, cwd=ROOT, capture_output=True, text=True, timeout=90
                )
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                result = subprocess.run(
                    [str(binary)], cwd=ROOT, capture_output=True, text=True, timeout=30
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("scalar roundtrips and guarded endings", result.stdout)
                print(f"{platform}: {result.stdout.strip()}")

    def test_first_app_provider_matches_generator_template(self):
        # Provider generation is this literal substitution in build_fonts().
        # This checks provider reproducibility, not bitmap/font regeneration.
        template = (LIBRARY / "tools/templates/nanoui_font_provider.cpp.in").read_text(
            encoding="utf-8"
        )
        expected = template.replace("@RESOURCE_HEADER@", "nanoui_assets.h")
        generated = LIBRARY / "examples/FirstApp/src/generated/nanoui_font_provider.cpp"
        self.assertEqual(generated.read_text(encoding="utf-8"), expected)


if __name__ == "__main__":
    unittest.main()
