#!/usr/bin/env python3
"""Small, offline engineering helpers driven by project.toml."""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path
from typing import Any, Sequence


ROOT = Path(__file__).resolve().parent.parent
PROJECT_FILE = "project.toml"
PROFILE_ENV = "ENGINEERING_PROFILE"
SUPPORTED_TEMPLATES = {
    "vue-k8s",
    "springboot-k8s",
    "flutter",
    "godot",
    "cpp-cmake",
    "zephyr",
    "hardware-altium",
    "hardware-lceda",
    "modeling",
    "blender",
}
DEPLOYABLE_TEMPLATES = {"vue-k8s": ("web", 80), "springboot-k8s": ("api", 8080)}
ID_RE = re.compile(r"^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$")
IMAGE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/:@+-]*$")


class ConfigError(ValueError):
    """A user-facing project or profile configuration error."""


def _fail(where: str, message: str) -> None:
    raise ConfigError(f"{where}: {message}")


def _safe_relative(root: Path, value: Any, where: str, *, file: bool | None = None) -> Path:
    if not isinstance(value, str) or not value.strip():
        _fail(where, "must be a non-empty repository-relative path")
    raw = Path(value)
    if raw.is_absolute() or ".." in raw.parts:
        _fail(where, "path must stay inside the repository")
    candidate = root / raw
    try:
        candidate.resolve(strict=False).relative_to(root.resolve())
    except ValueError:
        _fail(where, "path resolves outside the repository")
    if file is True and not candidate.is_file():
        _fail(where, f"file does not exist: {value}")
    if file is False and not candidate.is_dir():
        _fail(where, f"directory does not exist: {value}")
    return candidate


def read_toml(path: Path, where: str) -> dict[str, Any]:
    try:
        with path.open("rb") as handle:
            value = tomllib.load(handle)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        _fail(where, f"cannot read valid TOML ({exc})")
    if not isinstance(value, dict):
        _fail(where, "must contain a TOML table")
    return value


def _keys(table: dict[str, Any], allowed: set[str], where: str) -> None:
    extra = sorted(set(table) - allowed)
    if extra:
        _fail(where, f"unsupported key(s): {', '.join(extra)}")


def validate_project(root: Path = ROOT) -> dict[str, Any]:
    root = root.resolve()
    data = read_toml(root / PROJECT_FILE, PROJECT_FILE)
    _keys(data, {"schema_version", "project", "components", "deployment"}, PROJECT_FILE)
    if (
        not isinstance(data.get("schema_version"), int)
        or isinstance(data.get("schema_version"), bool)
        or data["schema_version"] != 1
    ):
        _fail(PROJECT_FILE, "schema_version must be 1")
    project = data.get("project")
    if not isinstance(project, dict):
        _fail(PROJECT_FILE, "[project] is required")
    _keys(project, {"id", "role", "release_branch"}, "[project]")
    project_id = project.get("id")
    if not isinstance(project_id, str) or len(project_id) > 63 or not ID_RE.fullmatch(project_id):
        _fail("project.id", "must be a lowercase DNS label of at most 63 characters")
    if not isinstance(project.get("role"), str) or project["role"] not in {
        "template-suite",
        "product",
    }:
        _fail("project.role", "must be 'template-suite' or 'product'")
    if not isinstance(project.get("release_branch"), str) or project["release_branch"] not in {
        "main",
        "prod",
    }:
        _fail("project.release_branch", "must be 'main' or 'prod'")

    components = data.get("components")
    if not isinstance(components, dict) or not components:
        _fail("[components]", "at least one component is required")
    seen_paths: set[str] = set()
    seen_ci: set[str] = set()
    for component_id, component in components.items():
        where = f"components.{component_id}"
        if (
            not isinstance(component_id, str)
            or not ID_RE.fullmatch(component_id)
            or len(component_id) > 40
        ):
            _fail(where, "component id must be a lowercase DNS label of at most 40 characters")
        if not isinstance(component, dict):
            _fail(where, "must be a table")
        _keys(component, {"template", "path", "ci", "tasks"}, where)
        template = component.get("template")
        if not isinstance(template, str) or template not in SUPPORTED_TEMPLATES:
            _fail(
                f"{where}.template",
                f"must name a supported component ({', '.join(sorted(SUPPORTED_TEMPLATES))})",
            )
        component_path = _safe_relative(root, component.get("path"), f"{where}.path", file=False)
        normalized_path = component_path.resolve().as_posix().lower()
        if normalized_path in seen_paths:
            _fail(where, "component path conflicts with another component")
        seen_paths.add(normalized_path)
        if "ci" in component and component["ci"] not in (None, "none"):
            ci_path = _safe_relative(root, component["ci"], f"{where}.ci", file=True)
            normalized_ci = ci_path.resolve().as_posix().lower()
            if normalized_ci in seen_ci:
                _fail(where, "CI path conflicts with another component")
            seen_ci.add(normalized_ci)
        tasks = component.get("tasks", {})
        if not isinstance(tasks, dict):
            _fail(f"{where}.tasks", "must be a table")
        for task, argv in tasks.items():
            task_where = f"{where}.tasks.{task}"
            if not isinstance(task, str) or not ID_RE.fullmatch(task):
                _fail(task_where, "task name must be a lowercase identifier")
            if (
                not isinstance(argv, list)
                or not argv
                or any(not isinstance(item, str) or "\x00" in item for item in argv)
            ):
                _fail(task_where, "must be a non-empty argv array of strings")
            if not argv[0]:
                _fail(task_where, "executable cannot be empty")
            if Path(argv[0]).name.lower() in {
                "python",
                "python.exe",
                "python3",
                "python3.exe",
                "py",
                "py.exe",
                "node",
                "node.exe",
                "bash",
                "sh",
                "pwsh",
                "pwsh.exe",
                "powershell",
                "powershell.exe",
            }:
                script = next(
                    (
                        item
                        for item in argv[1:]
                        if Path(item).suffix.lower()
                        in {".py", ".js", ".mjs", ".cjs", ".ps1", ".sh", ".bat", ".cmd"}
                    ),
                    None,
                )
                if script is not None:
                    if Path(script).is_absolute() or ".." in Path(script).parts:
                        _fail(task_where, "script path must stay inside the component directory")
                    _safe_relative(
                        root,
                        str(Path(component["path"]) / script),
                        task_where + " script",
                        file=True,
                    )

    if project["role"] == "template-suite" and set(components) != SUPPORTED_TEMPLATES:
        # The canonical suite keys intentionally equal template IDs.
        _fail("[components]", "template-suite must declare all ten supported component IDs")

    deployment = data.get("deployment", {"enabled": False})
    if not isinstance(deployment, dict):
        _fail("[deployment]", "must be a table")
    _keys(
        deployment,
        {"enabled", "namespace", "pull_secret", "ingress_class", "hosts"},
        "[deployment]",
    )
    enabled = deployment.get("enabled", False)
    if not isinstance(enabled, bool):
        _fail("deployment.enabled", "must be true or false")
    if enabled:
        if project["role"] != "product":
            _fail(
                "deployment.enabled",
                "Kubernetes rendering is supported for product repositories only",
            )
        for field in ("namespace", "pull_secret", "ingress_class"):
            value = deployment.get(field)
            if not isinstance(value, str) or not ID_RE.fullmatch(value) or len(value) > 63:
                _fail(f"deployment.{field}", "must be a lowercase DNS label")
        hosts = deployment.get("hosts")
        if not isinstance(hosts, dict):
            _fail("deployment.hosts", "must map enabled component IDs to hostnames")
        for component_id, component in components.items():
            template = component["template"]
            if template in DEPLOYABLE_TEMPLATES:
                host = hosts.get(component_id)
                if (
                    not isinstance(host, str)
                    or len(host) > 253
                    or not re.fullmatch(
                        r"(?=.{1,253}$)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)(?:\.(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?))*",
                        host,
                    )
                ):
                    _fail(
                        f"deployment.hosts.{component_id}", "must be a valid lowercase DNS hostname"
                    )
            elif component_id in hosts:
                _fail(
                    f"deployment.hosts.{component_id}",
                    "host supplied for a non-deployable component",
                )
        extra_hosts = set(hosts) - set(components)
        if extra_hosts:
            _fail("deployment.hosts", f"unknown component ID(s): {', '.join(sorted(extra_hosts))}")
    return data


def _profile_path() -> Path | None:
    value = os.environ.get(PROFILE_ENV)
    return Path(value).expanduser().resolve() if value else None


def load_profile() -> tuple[dict[str, Any], str | None, list[str]]:
    path = _profile_path()
    if not path:
        return {}, None, []
    if not path.is_file():
        return {}, str(path), [f"profile not found: {path}; using PATH discovery"]
    try:
        data = read_toml(path, f"profile {path}")
        # This helper consumes [tools] only. The same machine profile may carry
        # preferences/platform references used by the Lead; they must not make
        # otherwise valid executable paths silently fall back to PATH.
        tools = data.get("tools", {})
        if not isinstance(tools, dict):
            raise ConfigError("[tools] must be a table")
        valid: dict[str, list[str]] = {}
        notes: list[str] = []
        for name, values in tools.items():
            items = [values] if isinstance(values, str) else values
            if not isinstance(items, list) or not all(isinstance(x, str) for x in items):
                notes.append(f"ignored invalid tools.{name}; using PATH discovery")
                continue
            found = [str(Path(x).expanduser()) for x in items if Path(x).expanduser().is_file()]
            if found:
                valid[name] = found
            elif items:
                notes.append(f"no configured executable exists for {name}; using PATH discovery")
        return valid, str(path), notes
    except ConfigError as exc:
        return {}, str(path), [f"invalid profile: {exc}; using PATH discovery"]


def resolve_executable(
    name: str, profile_tools: dict[str, list[str]], cwd: Path | None = None
) -> tuple[str | None, str]:
    configured = profile_tools.get(name)
    if configured:
        return configured[0], "profile"
    path = shutil.which(name)
    # Maven wrapper is a checked-in script, rather than a globally installed tool.
    if name == "mvnw" and not path and cwd:
        candidate = cwd / ("mvnw.cmd" if os.name == "nt" else "mvnw")
        if candidate.is_file():
            return str(candidate), "project wrapper"
    return path, "PATH" if path else "missing"


def _version(executable: str) -> tuple[bool, str]:
    version_args = ["version"] if Path(executable).stem.lower() == "arduino-cli" else ["--version"]
    try:
        if os.name == "nt" and executable.lower().endswith((".cmd", ".bat")):
            env = os.environ.copy()
            env["ENGINEERING_TASK_JSON"] = json.dumps(
                {"exe": executable, "args": version_args}, ensure_ascii=True
            )
            script = "$j=ConvertFrom-Json $env:ENGINEERING_TASK_JSON; & $j.exe @($j.args)"
            shell = shutil.which("powershell.exe") or shutil.which("pwsh.exe") or "powershell.exe"
            result = subprocess.run(
                [shell, "-NoProfile", "-NonInteractive", "-Command", script],
                text=True,
                capture_output=True,
                timeout=8,
                check=False,
                env=env,
            )
            text = (result.stdout or result.stderr).strip().splitlines()
            return result.returncode == 0, (
                text[0][:240] if text else f"exit {result.returncode}, no version output"
            )
        result = subprocess.run(
            [executable, *version_args], text=True, capture_output=True, timeout=8, check=False
        )
        text = (result.stdout or result.stderr).strip().splitlines()
        return result.returncode == 0, (
            text[0][:240] if text else f"exit {result.returncode}, no version output"
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return False, f"unavailable ({exc.__class__.__name__})"


def doctor(project: dict[str, Any], component_filter: str | None) -> tuple[int, list[str]]:
    components = project["components"]
    if component_filter and component_filter not in components:
        return 2, [f"unknown component: {component_filter}"]
    selected = {component_filter: components[component_filter]} if component_filter else components
    tools, profile_path, notes = load_profile()
    lines = [
        f"Python: {platform.python_version()} ({sys.executable})",
        f"Platform: {platform.platform()}",
        f"Profile: {profile_path or 'not set; PATH discovery'}",
    ]
    lines.extend(f"Note: {note}" for note in notes)
    executables: dict[str, tuple[str | None, str]] = {}
    missing = False
    for component_id, component in selected.items():
        if not component.get("tasks"):
            lines.append(
                f"Component {component_id}: no tool probes configured; capability unverified"
            )
            missing = True
        for argv in component.get("tasks", {}).values():
            tool_name = argv[0]
            if tool_name in {"mvnw", "mvnw.cmd"}:
                tool_name = "mvnw"
            if tool_name not in executables:
                component_cwd = ROOT / component["path"]
                executable, source = resolve_executable(tool_name, tools, component_cwd)
                executables[tool_name] = (executable, source)
        lines.append(f"Component {component_id}: {component['template']} at {component['path']}")
    for name, (executable, source) in sorted(executables.items()):
        if executable:
            ok, version = _version(executable)
            missing = missing or not ok
            lines.append(
                f"Tool {name}: {executable} [{source}] — {version}"
                + (" (version probe failed)" if not ok else "")
            )
        else:
            missing = True
            lines.append(f"Tool {name}: missing from profile and PATH")
    return (1 if missing else 0), lines


def _windows_batch_run(executable: str, argv: Sequence[str], cwd: Path) -> int:
    # Keep the PowerShell program constant; executable and argv arrive as JSON data.
    script = (
        "$j=ConvertFrom-Json $env:ENGINEERING_TASK_JSON; & $j.exe @($j.args); exit $LASTEXITCODE"
    )
    env = os.environ.copy()
    env["ENGINEERING_TASK_JSON"] = json.dumps(
        {"exe": executable, "args": list(argv)}, ensure_ascii=True
    )
    shell = shutil.which("powershell.exe") or shutil.which("pwsh.exe") or "powershell.exe"
    return subprocess.run(
        [shell, "-NoProfile", "-NonInteractive", "-Command", script], cwd=cwd, env=env, check=False
    ).returncode


def run_task(project: dict[str, Any], component_id: str, task: str, extra: Sequence[str]) -> int:
    component = project["components"].get(component_id)
    if not component:
        print(f"unknown component: {component_id}", file=sys.stderr)
        return 2
    argv = component.get("tasks", {}).get(task)
    if not argv:
        print(f"unknown task {task!r} for component {component_id}", file=sys.stderr)
        return 2
    tools, profile_path, notes = load_profile()
    for note in notes:
        print(f"Note: {note}", file=sys.stderr)
    tool_name = "mvnw" if argv[0] in {"mvnw", "mvnw.cmd"} else argv[0]
    cwd = (ROOT / component["path"]).resolve()
    executable, source = resolve_executable(tool_name, tools, cwd)
    if not executable:
        print(
            f"missing executable {tool_name!r} (profile: {profile_path or 'none'}, PATH fallback unavailable)",
            file=sys.stderr,
        )
        return 127
    args = [*argv[1:], *extra]
    print(f"Running {component_id}.{task} in {cwd} using {executable} [{source}]", flush=True)
    try:
        if os.name == "nt" and executable.lower().endswith((".cmd", ".bat")):
            return _windows_batch_run(executable, args, cwd)
        return subprocess.run([executable, *args], cwd=cwd, check=False).returncode
    except OSError as exc:
        print(f"cannot start task: {exc}", file=sys.stderr)
        return 127


def _images(values: Sequence[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values:
        if "=" not in value:
            _fail("--image", "use component=registry:tag")
        component_id, image = value.split("=", 1)
        final_segment = image.rsplit("/", 1)[-1]
        has_tag = ":" in final_segment and not final_segment.endswith(":")
        has_digest = bool(re.search(r"@sha256:[0-9a-f]{64}$", image))
        if (
            not ID_RE.fullmatch(component_id)
            or not IMAGE_RE.fullmatch(image)
            or not (has_tag or has_digest)
        ):
            _fail("--image", f"invalid component/image mapping: {value}")
        if component_id in result:
            _fail("--image", f"duplicate image for component {component_id}")
        result[component_id] = image
    return result


def render(
    project: dict[str, Any], image_args: Sequence[str], version: str | None = None
) -> dict[str, Any]:
    deployment = project.get("deployment", {})
    if not deployment.get("enabled", False):
        _fail("deployment.enabled", "must be true before rendering manifests")
    images = _images(image_args)
    if version is not None and not re.fullmatch(r"[A-Za-z0-9._-]{1,128}", version):
        _fail("--version", "must be a short non-empty version or commit identifier")
    components = project["components"]
    deployable = [
        (key, value, DEPLOYABLE_TEMPLATES[value["template"]])
        for key, value in components.items()
        if value["template"] in DEPLOYABLE_TEMPLATES
    ]
    if not deployable:
        _fail("[components]", "no selected Vue or Spring Boot component can be rendered")
    expected = {key for key, _, _ in deployable}
    if set(images) != expected:
        missing = sorted(expected - images.keys())
        extra = sorted(images.keys() - expected)
        parts = []
        if missing:
            parts.append("missing image(s): " + ", ".join(missing))
        if extra:
            parts.append("unexpected image(s): " + ", ".join(extra))
        _fail("--image", "; ".join(parts))

    resources: list[dict[str, Any]] = []
    project_id = project["project"]["id"]
    seen_hosts: set[str] = set()
    for component_id, component, (_, service_port) in deployable:
        unique = f"{project_id}-{component_id}"
        if len(unique) > 63:
            _fail("project.id", f"generated Kubernetes label is too long for {component_id}")
        labels = {"app.kubernetes.io/name": unique}
        selector = dict(labels)
        health_path = (
            "/healthz" if component["template"] == "vue-k8s" else "/actuator/health/readiness"
        )
        host = deployment["hosts"][component_id]
        if host in seen_hosts:
            _fail("deployment.hosts", f"host is assigned to more than one component: {host}")
        seen_hosts.add(host)
        resources.extend(
            [
                {
                    "apiVersion": "apps/v1",
                    "kind": "Deployment",
                    "metadata": {
                        "name": unique,
                        "namespace": deployment["namespace"],
                        "labels": labels,
                    },
                    "spec": {
                        "replicas": 1,
                        "selector": {"matchLabels": selector},
                        "template": {
                            "metadata": {"labels": selector},
                            "spec": {
                                "imagePullSecrets": [{"name": deployment["pull_secret"]}],
                                "containers": [
                                    {
                                        "name": component_id,
                                        "image": images[component_id],
                                        "imagePullPolicy": "Always",
                                        "ports": [{"name": "http", "containerPort": service_port}],
                                        "env": [
                                            {
                                                "name": "APP_VERSION",
                                                "value": version
                                                or images[component_id].rsplit(":", 1)[-1],
                                            }
                                        ],
                                        "resources": {
                                            "requests": {"cpu": "50m", "memory": "128Mi"},
                                            "limits": {"cpu": "1", "memory": "1Gi"},
                                        },
                                        "readinessProbe": {
                                            "httpGet": {"path": health_path, "port": "http"},
                                            "initialDelaySeconds": 5,
                                            "periodSeconds": 5,
                                        },
                                        "startupProbe": {
                                            "httpGet": {"path": health_path, "port": "http"},
                                            "failureThreshold": 60,
                                            "periodSeconds": 5,
                                        },
                                    }
                                ],
                            },
                        },
                    },
                },
                {
                    "apiVersion": "v1",
                    "kind": "Service",
                    "metadata": {
                        "name": unique,
                        "namespace": deployment["namespace"],
                        "labels": labels,
                    },
                    "spec": {
                        "type": "ClusterIP",
                        "selector": selector,
                        "ports": [{"name": "http", "port": service_port, "targetPort": "http"}],
                    },
                },
                {
                    "apiVersion": "networking.k8s.io/v1",
                    "kind": "Ingress",
                    "metadata": {
                        "name": unique,
                        "namespace": deployment["namespace"],
                        "labels": labels,
                    },
                    "spec": {
                        "ingressClassName": deployment["ingress_class"],
                        "rules": [
                            {
                                "host": host,
                                "http": {
                                    "paths": [
                                        {
                                            "path": "/",
                                            "pathType": "Prefix",
                                            "backend": {
                                                "service": {
                                                    "name": unique,
                                                    "port": {"number": service_port},
                                                }
                                            },
                                        }
                                    ]
                                },
                            }
                        ],
                    },
                },
            ]
        )
    return {"apiVersion": "v1", "kind": "List", "items": resources}


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check", help="validate project.toml and its referenced files")
    doctor_parser = commands.add_parser("doctor", help="report available component tools")
    doctor_parser.add_argument("--component")
    run_parser = commands.add_parser("run", help="run a named task with explicit argv")
    run_parser.add_argument("component")
    run_parser.add_argument("task")
    run_parser.add_argument("args", nargs=argparse.REMAINDER)
    render_parser = commands.add_parser(
        "render", help="write Kubernetes JSON for an enabled product"
    )
    render_parser.add_argument("--output", required=True, type=Path)
    render_parser.add_argument(
        "--image", action="append", default=[], metavar="COMPONENT=REGISTRY:TAG"
    )
    render_parser.add_argument(
        "--version", help="code version/commit, independent of the immutable image digest"
    )
    args = parser.parse_args(argv)
    try:
        project = validate_project()
        if args.command == "check":
            print(
                f"Project contract passed: {project['project']['id']} ({project['project']['role']})"
            )
            return 0
        if args.command == "doctor":
            code, lines = doctor(project, args.component)
            print("\n".join(lines))
            return code
        if args.command == "run":
            extra = args.args[1:] if args.args[:1] == ["--"] else args.args
            return run_task(project, args.component, args.task, extra)
        if args.command == "render":
            manifest = render(project, args.image, args.version)
            output = args.output.expanduser().resolve()
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            print(f"Wrote {len(manifest['items'])} Kubernetes resources to {output}")
            return 0
    except ConfigError as exc:
        print(f"engineering: {exc}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
