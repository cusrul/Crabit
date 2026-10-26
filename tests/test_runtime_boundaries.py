from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import engineering


ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("ci_deploy", ROOT / "ci/deploy.py")
deploy = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(deploy)


def deployment_project(project_id: str = "project-one") -> dict:
    return {
        "project": {"id": project_id, "role": "product"},
        "components": {"web": {"template": "vue-k8s"}},
        "deployment": {
            "enabled": True,
            "namespace": "dev",
            "pull_secret": "registry-pull",
            "ingress_class": "nginx",
            "hosts": {"web": f"{project_id}.example.test"},
        },
    }


class RuntimeBoundaryTests(unittest.TestCase):
    def test_doctor_marks_unprobed_and_failed_version_checks_unsuccessful(self):
        unprobed = {"components": {"assets": {"template": "modeling", "path": "assets"}}}
        code, lines = engineering.doctor(unprobed, "assets")
        self.assertNotEqual(code, 0)
        self.assertTrue(any("no tool probes configured" in line for line in lines))

        project = {"components": {"web": {"template": "vue-k8s", "path": "web", "tasks": {"build": ["npm", "run", "build"]}}}}
        failed = subprocess.CompletedProcess(["npm", "--version"], 3, stdout="npm failed", stderr="")
        with mock.patch.object(engineering, "load_profile", return_value=({}, None, [])), \
             mock.patch.object(engineering, "resolve_executable", return_value=("npm", "PATH")), \
             mock.patch.object(engineering.subprocess, "run", return_value=failed):
            code, lines = engineering.doctor(project, "web")
        self.assertNotEqual(code, 0)
        self.assertTrue(any("version probe failed" in line for line in lines))

    def test_different_project_ids_produce_disjoint_deployment_selectors(self):
        images = ["web=registry.example.test/app/web:abc123"]
        first = engineering.render(deployment_project("project-one"), images)
        second = engineering.render(deployment_project("project-two"), images)
        first_selectors = {
            item["spec"]["selector"]["matchLabels"]["app.kubernetes.io/name"]
            for item in first["items"] if item["kind"] == "Deployment"
        }
        second_selectors = {
            item["spec"]["selector"]["matchLabels"]["app.kubernetes.io/name"]
            for item in second["items"] if item["kind"] == "Deployment"
        }
        self.assertTrue(first_selectors)
        self.assertTrue(second_selectors)
        self.assertTrue(first_selectors.isdisjoint(second_selectors))

    def write_product(self, root: Path, *, enabled: bool, namespace: str = "dev") -> None:
        (root / "api").mkdir(exist_ok=True)
        (root / "project.toml").write_text(
            f'''schema_version = 1
[project]
id = "boundary-test"
role = "product"
release_branch = "main"
[components.api]
template = "springboot-k8s"
path = "api"
[deployment]
enabled = {str(enabled).lower()}
namespace = "{namespace}"
pull_secret = "registry-pull"
ingress_class = "nginx"
[deployment.hosts]
api = "boundary.example.test"
''',
            encoding="utf-8",
        )

    def test_deploy_refuses_disabled_or_non_dev_scope_before_external_calls(self):
        cases = ((True, "dev", "feature"), (True, "staging", "dev"), (False, "dev", "dev"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(deploy, "ROOT", root), \
                 mock.patch.object(deploy, "kubectl") as kubectl, \
                 mock.patch.object(deploy.subprocess, "run") as run, \
                 mock.patch.object(deploy.urllib.request, "urlopen") as urlopen:
                for enabled, namespace, branch in cases:
                    with self.subTest(enabled=enabled, namespace=namespace, branch=branch):
                        self.write_product(root, enabled=enabled, namespace=namespace)
                        with mock.patch.dict(os.environ, {"CI_COMMIT_BRANCH": branch, "KUBE_CONTEXT": "test-context"}):
                            with self.assertRaises(deploy.ConfigError):
                                deploy.main()
                        kubectl.assert_not_called()
                        run.assert_not_called()
                        urlopen.assert_not_called()

    def test_kube_ca_only_changes_cluster_trust_when_explicitly_set(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_product(root, enabled=True)
            (root / "springboot-image.digest").write_text("sha256:" + "a" * 64)

            def fake_kubectl(*args, capture=False):
                if args[:2] == ("config", "view"):
                    return "agent-cluster"
                if "ingress" in args and capture:
                    return json.dumps({"status": {"loadBalancer": {"ingress": [{"ip": "127.0.0.1"}]}}})
                return ""

            response = mock.MagicMock()
            response.read.return_value = b'{"status":"UP"}'
            response.__enter__.return_value = response
            env = {
                "CI_COMMIT_BRANCH": "dev",
                "CI_REGISTRY_IMAGE": "registry.example.test/group/boundary-test",
                "CI_COMMIT_SHA": "abc123",
                "KUBE_CONTEXT": "test-context",
            }
            for ca_file in (None, "C:/agent/gitlab-ca.crt"):
                with self.subTest(ca_file=ca_file), \
                     mock.patch.object(deploy, "ROOT", root), \
                     mock.patch.object(deploy, "kubectl", side_effect=fake_kubectl) as kubectl, \
                     mock.patch.object(deploy.subprocess, "run"), \
                     mock.patch.object(deploy.urllib.request, "urlopen", return_value=response):
                    values = dict(env)
                    if ca_file is not None:
                        values["KUBE_CA_FILE"] = ca_file
                    with mock.patch.dict(os.environ, values, clear=False):
                        os.environ.pop("KUBE_CA_FILE", None)
                        if ca_file is not None:
                            os.environ["KUBE_CA_FILE"] = ca_file
                        deploy.main()
                    calls = [call.args for call in kubectl.call_args_list]
                    set_cluster = [args for args in calls if args[:2] == ("config", "set-cluster")]
                    view = [args for args in calls if args[:2] == ("config", "view")]
                    self.assertEqual(bool(set_cluster), ca_file is not None)
                    self.assertEqual(bool(view), ca_file is not None)
                    if ca_file is not None:
                        self.assertIn("--certificate-authority=" + ca_file, set_cluster[0])


if __name__ == "__main__":
    unittest.main()
