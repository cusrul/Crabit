import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("ci_prepare", ROOT / "ci/prepare.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CIContractTests(unittest.TestCase):
    def cpp_fixture(self, root, *, duplicate_selected=False):
        components = [("workflow", "tools/workflow", "ci/product-contract.yml", "cpp-cmake")]
        components.extend([
            ("stm32", "firmware/stm32", "none", "cpp-cmake"),
            ("bw16", "firmware/bw16", "none", "cpp-cmake"),
            ("mobile", "apps/mobile", "none", "flutter"),
        ])
        includes = ["ci/native-codex-workflow.yml", "ci/deploy.yml", "ci/product-contract.yml"]
        if duplicate_selected:
            components.append(("other-workflow", "tools/other-workflow", "ci/other-contract.yml", "cpp-cmake"))
            includes.append("ci/other-contract.yml")
        (root / "ci").mkdir(exist_ok=True)
        for component_id, path, ci, template in components:
            (root / path).mkdir(parents=True, exist_ok=True)
            if ci != "none":
                (root / ci).write_text(f"{component_id}: {{}}")
        project = '''schema_version = 1
[project]
id = "crabit-test"
role = "product"
release_branch = "main"
'''
        for component_id, path, ci, template in components:
            ci_value = f'"{ci}"'
            project += f'\n[components.{component_id}]\ntemplate = "{template}"\npath = "{path}"\nci = {ci_value}\n'
        project += '\n[deployment]\nenabled = false\n'
        (root / "project.toml").write_text(project)
        (root / ".gitlab-ci.yml").write_text("include:\n" + "".join(f"  - local: {item}\n" for item in includes))

    def fixture(self, root):
        (root / "web").mkdir()
        (root / "web/package.json").write_text("{}")
        (root / "ci").mkdir()
        (root / "ci/vue-k8s.yml").write_text("vue-verify: {}")
        (root / "project.toml").write_text('''schema_version=1
[project]
id="example"
role="product"
release_branch="prod"
[components.frontend]
template="vue-k8s"
path="web"
ci="ci/vue-k8s.yml"
[deployment]
enabled=false
''')
        (root / ".gitlab-ci.yml").write_text('''include:
  - local: ci/native-codex-workflow.yml
  - local: ci/deploy.yml
  - local: ci/vue-k8s.yml
''')

    def test_custom_component_paths_images_and_legacy_release_mapping(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root)
            values = module.prepare(root, {"CI_REGISTRY_IMAGE": "registry.example/team/project", "PROJECT_ROLE": "product"})
            self.assertEqual(values["VUE_DIR"], "web")
            self.assertEqual(values["VUE_IMAGE_REPOSITORY"], "registry.example/team/project/frontend")
            self.assertEqual(module.validate_project(root)["project"]["release_branch"], "prod")

    def test_deployment_switch_drift_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root)
            with self.assertRaisesRegex(module.ConfigError, "DEPLOY_DEV_ENABLED"):
                module.prepare(root, {"PROJECT_ROLE": "product", "DEPLOY_DEV_ENABLED": "true"})

    def test_role_drift_fails_before_build(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root)
            with self.assertRaisesRegex(module.ConfigError, "PROJECT_ROLE"):
                module.prepare(root, {"PROJECT_ROLE": "template-suite"})

    def test_unselected_component_include_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root)
            with (root / ".gitlab-ci.yml").open("a") as handle:
                handle.write("  - local: ci/springboot-k8s.yml\n")
            with self.assertRaisesRegex(module.ConfigError, "CI includes"):
                module.prepare(root, {"PROJECT_ROLE": "product"})

    def test_missing_selected_include_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root)
            (root / ".gitlab-ci.yml").write_text("include:\n  - local: ci/native-codex-workflow.yml\n  - local: ci/deploy.yml\n")
            with self.assertRaisesRegex(module.ConfigError, "CI includes"):
                module.prepare(root, {"PROJECT_ROLE": "product"})

    def test_cpp_components_without_ci_are_not_selected_for_dotenv_bindings(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.cpp_fixture(root)
            values = module.prepare(root, {"PROJECT_ROLE": "product"})
            self.assertEqual(values["CPP_DIR"], "tools/workflow")
            self.assertNotIn("FLUTTER_DIR", values)

    def test_duplicate_selected_cpp_components_still_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.cpp_fixture(root, duplicate_selected=True)
            with self.assertRaisesRegex(module.ConfigError, "one component per template"):
                module.prepare(root, {"PROJECT_ROLE": "product"})


if __name__ == "__main__":
    unittest.main()
