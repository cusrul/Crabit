from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import engineering
from tools import verify_native_workflow


class EngineeringTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "src").mkdir()
        (self.root / "src/web").mkdir()
        (self.root / "src/web/build.py").write_text("pass\n", encoding="utf-8")
        (self.root / "ci").mkdir()
        (self.root / "ci/check.yml").write_text("job: check\n", encoding="utf-8")
        self.write_project()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write_project(self, *, role: str = "product", project_id: str = "demo-shop", path: str = "src/web",
                      ci: str = "ci/check.yml", enabled: bool = False, hosts: str = "") -> None:
        (self.root / "project.toml").write_text(
            f'''schema_version = 1
[project]
id = "{project_id}"
role = "{role}"
release_branch = "main"
[components.web]
template = "vue-k8s"
path = "{path}"
ci = "{ci}"
[components.web.tasks]
build = ["python", "build.py"]
[components.api]
template = "springboot-k8s"
path = "src/api"
ci = "ci/api.yml"
[deployment]
enabled = {str(enabled).lower()}
namespace = "dev"
pull_secret = "private-registry"
ingress_class = "nginx"
{hosts}''', encoding="utf-8")
        (self.root / "src/api").mkdir(exist_ok=True)
        (self.root / "ci/api.yml").write_text("api: true\n", encoding="utf-8")

    def test_product_does_not_need_templates_directory(self) -> None:
        project = engineering.validate_project(self.root)
        self.assertEqual(project["project"]["role"], "product")
        self.assertFalse((self.root / "templates").exists())

    def test_rejects_path_traversal_and_symlink_escape(self) -> None:
        self.write_project(path="../outside")
        with self.assertRaisesRegex(engineering.ConfigError, "stay inside"):
            engineering.validate_project(self.root)
        self.write_project(path="src/escape")
        outside = Path(self.temp.name).parent / (Path(self.temp.name).name + "-outside")
        outside.mkdir(exist_ok=True)
        try:
            (self.root / "src/escape").symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(engineering.ConfigError, "resolves outside"):
                engineering.validate_project(self.root)
        finally:
            outside.rmdir()

    def test_missing_ci_and_duplicate_toml_keys_fail(self) -> None:
        self.write_project(ci="ci/missing.yml")
        with self.assertRaisesRegex(engineering.ConfigError, "file does not exist"):
            engineering.validate_project(self.root)
        (self.root / "project.toml").write_text('schema_version=1\nschema_version=1\n', encoding="utf-8")
        with self.assertRaisesRegex(engineering.ConfigError, "valid TOML"):
            engineering.validate_project(self.root)

    def test_missing_or_escaping_task_script_fails(self) -> None:
        (self.root / "src/web/build.py").unlink()
        with self.assertRaisesRegex(engineering.ConfigError, "script.*does not exist"):
            engineering.validate_project(self.root)
        self.write_project()
        (self.root / "project.toml").write_text(
            (self.root / "project.toml").read_text(encoding="utf-8").replace('"build.py"', '"../outside.py"'),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(engineering.ConfigError, "script path must stay"):
            engineering.validate_project(self.root)

    def test_deployment_config_requires_hosts_and_render_requires_enabled(self) -> None:
        project = engineering.validate_project(self.root)
        with self.assertRaisesRegex(engineering.ConfigError, "must be true"):
            engineering.render(project, ["web=registry/web:dev", "api=registry/api:dev"])
        self.write_project(enabled=True)
        with self.assertRaisesRegex(engineering.ConfigError, "deployment.hosts"):
            engineering.validate_project(self.root)

    def test_render_has_unique_names_selectors_hosts_and_pull_secret(self) -> None:
        self.write_project(enabled=True, hosts='[deployment.hosts]\nweb = "web.demo.test"\napi = "api.demo.test"\n')
        project = engineering.validate_project(self.root)
        result = engineering.render(project, ["web=registry/web:dev", "api=registry/api:dev"])
        items = result["items"]
        names = [(item["kind"], item["metadata"]["name"]) for item in items]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual([item["metadata"]["name"] for item in items[::3]], ["demo-shop-web", "demo-shop-api"])
        deployments = [item for item in items if item["kind"] == "Deployment"]
        services = [item for item in items if item["kind"] == "Service"]
        self.assertEqual(deployments[0]["spec"]["selector"]["matchLabels"], services[0]["spec"]["selector"])
        self.assertEqual(deployments[0]["spec"]["template"]["spec"]["imagePullSecrets"], [{"name": "private-registry"}])
        ingresses = [item for item in items if item["kind"] == "Ingress"]
        self.assertEqual([item["spec"]["rules"][0]["host"] for item in ingresses], ["web.demo.test", "api.demo.test"])

    def test_missing_or_bad_profile_falls_back_to_path(self) -> None:
        with mock.patch.dict(os.environ, {"ENGINEERING_PROFILE": str(self.root / "missing.toml")}):
            tools, _, notes = engineering.load_profile()
        self.assertEqual(tools, {})
        self.assertIn("using PATH", notes[0])
        bad = self.root / "profile.toml"
        bad.write_text(f'[tools]\npython = "{self.root / "does-not-exist"}"\n', encoding="utf-8")
        with mock.patch.dict(os.environ, {"ENGINEERING_PROFILE": str(bad)}):
            tools, _, notes = engineering.load_profile()
        self.assertNotIn("python", tools)
        self.assertIn("using PATH", notes[0])
        executable, source = engineering.resolve_executable("python", tools)
        self.assertTrue(executable)
        self.assertEqual(source, "PATH")

    def test_shared_machine_profile_retains_tools_with_advisory_sections(self) -> None:
        profile = self.root / "shared-profile.toml"
        executable = Path(os.sys.executable).as_posix()
        profile.write_text('[tools]\npython = ' + json.dumps(executable) +
                           '\n[preferences]\nshell = "powershell"\n'
                           '[platform]\nagent_context = "workspace/test:cluster"\n', encoding="utf-8")
        with mock.patch.dict(os.environ, {"ENGINEERING_PROFILE": str(profile)}):
            tools, path, notes = engineering.load_profile()
        self.assertEqual(path, str(profile))
        self.assertEqual([Path(value) for value in tools["python"]], [Path(executable)])
        self.assertEqual(notes, [])

    def test_arduino_cli_version_probe_uses_version_subcommand(self) -> None:
        completed = mock.Mock(returncode=0, stdout="arduino-cli Version: 1.0.0", stderr="")
        with mock.patch.object(engineering.subprocess, "run", return_value=completed) as run:
            ok, version = engineering._version("/usr/bin/arduino-cli")
        self.assertTrue(ok)
        self.assertEqual(version, "arduino-cli Version: 1.0.0")
        self.assertEqual(run.call_args.args[0], ["/usr/bin/arduino-cli", "version"])

    def test_run_passes_arguments_as_data_without_shell(self) -> None:
        capture = self.root / "capture.py"
        capture.write_text("import json,sys\nopen('args.json','w').write(json.dumps(sys.argv[1:]))\n", encoding="utf-8")
        project = {"components": {"web": {"path": "src/web", "tasks": {"build": [os.fspath(Path(os.sys.executable)), os.fspath(capture)]}}}}
        tricky = "$(touch pwned); & 'quoted'"
        with mock.patch.object(engineering, "ROOT", self.root):
            code = engineering.run_task(project, "web", "build", [tricky])
        self.assertEqual(code, 0)
        self.assertEqual(json.loads((self.root / "src/web/args.json").read_text(encoding="utf-8")), [tricky])
        self.assertFalse((self.root / "src/web/pwned").exists())

    def test_windows_cmd_task_keeps_arguments_out_of_command_text(self) -> None:
        project = {"components": {"web": {"path": "src/web", "tasks": {"build": ["npm", "run", "build"]}}}}
        captured: dict[str, object] = {}

        def fake_run(argv, **kwargs):
            captured["argv"] = argv
            captured["env"] = kwargs["env"]
            return mock.Mock(returncode=0)

        with mock.patch.object(engineering, "ROOT", self.root), \
             mock.patch.object(engineering.os, "name", "nt"), \
             mock.patch.object(engineering, "resolve_executable", return_value=("C:/node/npm.cmd", "profile")), \
             mock.patch.object(engineering.subprocess, "run", side_effect=fake_run):
            code = engineering.run_task(project, "web", "build", ["&calc.exe", "$(no)"])
        self.assertEqual(code, 0)
        self.assertNotIn("&calc.exe", captured["argv"][-1])
        self.assertEqual(json.loads(captured["env"]["ENGINEERING_TASK_JSON"])["args"], ["run", "build", "&calc.exe", "$(no)"])

    def test_verifier_rejects_unselected_local_ci_includes(self) -> None:
        (self.root / ".gitlab-ci.yml").write_text(
            "include:\n  - local: ci/native-codex-workflow.yml\n  - local: ci/deploy.yml\n  - local: ci/check.yml\n  - local: ci/api.yml\n  - local: ci/flutter.yml\n",
            encoding="utf-8",
        )
        with mock.patch.object(verify_native_workflow, "ROOT", self.root):
            errors: list[str] = []
            verify_native_workflow.check_ci_includes(errors)
        self.assertTrue(any("unselected" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
