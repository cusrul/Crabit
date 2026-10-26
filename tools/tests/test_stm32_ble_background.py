"""Run the real STM32 companion BLE implementation against narrow host doubles."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/stm32/Core/Src/companion_ble.cpp"
HARNESS = ROOT / "tools/tests/stm32_ble_background.cpp"
SHARED = ROOT / "firmware/shared"
STM32_INC = ROOT / "firmware/stm32/Core/Inc"


class Stm32BleBackgroundTests(unittest.TestCase):
    def test_actual_production_body_and_shared_policy(self):
        compiler = next(
            (
                p
                for p in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++"))
                if p and Path(p).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "an existing local g++ is required")
        source = SOURCE.read_text(encoding="utf-8")
        services = (STM32_INC / "sys_services.h").read_text(encoding="utf-8")
        self.assertIn("Crabit::CompanionBle::observeModeStatus(payload);", services)
        # Static source guard: the legacy CMake switch may still be defined,
        # but the production implementation must no longer contain its branch
        # or any ATTACH wire command. The executable fixture below separately
        # proves identical runtime behavior with the macro set to both values.
        self.assertNotIn("#if CRABIT_DIAG_SKIP_ATTACH", source)
        self.assertNotIn('"ATTACH"', source)
        cmake = (ROOT / "firmware/stm32/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('option(CRABIT_DIAG_SKIP_ATTACH "Legacy no-op:', cmake)
        # Replace only platform/UI include directives in this temporary fixture.
        # Both shared product policy headers are included unchanged from disk.
        replaced = []
        lines = []
        for line in source.splitlines():
            if line.startswith('#include "'):
                name = line.split('"')[1]
                if name in (
                    "../../../shared/companion_session.h",
                    "../../../shared/companion_probe.h",
                ):
                    lines.append(f'#include "{Path(name).name}"')
                else:
                    replaced.append(name)
            else:
                lines.append(line)
        self.assertEqual(
            set(replaced),
            {
                "companion_ble.h",
                "companion_trace.h",
                "app_real/app_coproc.h",
                "app_real/app_cyber_link.h",
                "app_real/components/companion_pair_prompt.h",
                "ameba_session.h",
                "sys_power.h",
            },
        )
        with tempfile.TemporaryDirectory(prefix="crabit-stm32-ble-host-") as tmp:
            tmp = Path(tmp)
            (tmp / "companion_ble_production_fixture.cpp").write_text(
                f'#include "{HARNESS.as_posix()}"\n' + "\n".join(lines) + "\n", encoding="utf-8"
            )
            for skip_attach in (0, 1):
                with self.subTest(skip_attach=skip_attach):
                    binary = tmp / f"stm32-ble-background-{skip_attach}.exe"
                    build = subprocess.run(
                        [
                            compiler,
                            "-std=c++11",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            f"-DCRABIT_DIAG_SKIP_ATTACH={skip_attach}",
                            "-I",
                            str(tmp),
                            "-I",
                            str(SHARED),
                            "-I",
                            str(STM32_INC),
                            str(tmp / "companion_ble_production_fixture.cpp"),
                            "-o",
                            str(binary),
                        ],
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                    run = subprocess.run([str(binary)], capture_output=True, text=True)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertIn("STM32 BLE background production assertions passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
