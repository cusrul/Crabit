"""Compile and execute host-only assertions against the real session policy header."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tools/tests/companion_session_test.cpp"
HEADER = ROOT / "firmware/shared/companion_session.h"


class CompanionSessionPolicyTests(unittest.TestCase):
    def test_pure_policy_header_with_real_host_assertions(self):
        compiler = next(
            (
                p
                for p in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++"))
                if p and Path(p).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "an existing local g++ is required")
        self.assertTrue(HEADER.is_file())
        self.assertTrue(SOURCE.is_file())
        with tempfile.TemporaryDirectory(prefix="crabit-companion-session-") as tmp:
            binary = Path(tmp) / "companion-session-host.exe"
            build = subprocess.run(
                [
                    compiler,
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(HEADER.parent),
                    str(SOURCE),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("companion session policy assertions passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
