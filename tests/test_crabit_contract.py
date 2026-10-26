"""Exercise the actual product's deployment and command boundaries."""
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path
from unittest import mock

from tools import engineering


ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("crabit_deploy", ROOT / "ci/deploy.py")
deploy = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(deploy)


class CrabitContractTests(unittest.TestCase):
    def test_actual_firmware_product_cannot_deploy(self):
        with mock.patch.object(deploy, "kubectl") as kubectl, \
             mock.patch.object(deploy.subprocess, "run") as run, \
             mock.patch.object(deploy.urllib.request, "urlopen") as urlopen:
            with self.assertRaisesRegex(deploy.ConfigError, "enabled product"):
                deploy.main()
            kubectl.assert_not_called()
            run.assert_not_called()
            urlopen.assert_not_called()

    def test_undeclared_flash_task_never_starts_a_tool(self):
        project = engineering.validate_project(ROOT)
        with mock.patch.object(engineering, "resolve_executable") as resolve, \
             mock.patch.object(engineering.subprocess, "run") as run:
            self.assertEqual(engineering.run_task(project, "bw16", "flash", []), 2)
            resolve.assert_not_called()
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
