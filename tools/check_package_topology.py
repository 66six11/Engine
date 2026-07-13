#!/usr/bin/env python3
"""Validate source-boundary manifests and emit a deterministic topology inventory."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any


MANIFEST_NAME = "asharia.package.json"
IGNORED_PATH_PARTS = {".git", "build", "generated"}
ALLOWED_SOURCE_ROLES = {
    "engine-component",
    "host-application",
    "module-group",
    "tool",
}
ALLOWED_OWNER_DOMAINS = {
    "content",
    "data",
    "editor",
    "foundation",
    "observability",
    "platform",
    "product",
    "rendering",
    "world",
}
ALLOWED_TARGET_ROLES = {
    "adapter",
    "backend",
    "bootstrap",
    "compatibility",
    "content-build",
    "contract",
    "diagnostics",
    "host",
    "runtime",
    "test",
    "tool",
}
CMAKE_TARGET_PATTERN = re.compile(
    r"\b(?P<command>add_library|add_executable|add_custom_target)"
    r"\s*\(\s*(?P<target>\"[^\"]+\"|[^\s\)]+)",
    re.IGNORECASE,
)


def _error(errors: list[str], path: Path, message: str) -> None:
    errors.append(f"{path.as_posix()}: {message}")


def _string_list(
    manifest: dict[str, Any], field: str, path: Path, errors: list[str]
) -> list[str]:
    value = manifest.get(field, [])
    if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
        _error(errors, path, f"{field} must be an array of non-empty strings")
        return []
    if len(value) != len(set(value)):
        _error(errors, path, f"{field} contains duplicate entries")
    return value


def _discover_manifests(root: Path) -> list[Path]:
    manifests: list[Path] = []
    for path in root.rglob(MANIFEST_NAME):
        relative = path.relative_to(root)
        if any(part in IGNORED_PATH_PARTS for part in relative.parts):
            continue
        manifests.append(path)
    return sorted(manifests, key=lambda item: item.relative_to(root).as_posix())


def _declared_cmake_targets(package_root: Path) -> set[str]:
    targets: set[str] = set()
    for cmake_path in sorted(package_root.rglob("CMakeLists.txt")):
        text = cmake_path.read_text(encoding="utf-8")
        text = re.sub(r"(?m)#.*$", "", text)
        for match in CMAKE_TARGET_PATTERN.finditer(text):
            target = match.group("target").strip('"')
            if not target or "$" in target or "::" in target:
                continue
            targets.add(target)
    return targets


def _validate_cycle(graph: dict[str, list[str]], errors: list[str]) -> None:
    state: dict[str, int] = {}
    stack: list[str] = []
    reported: set[tuple[str, ...]] = set()

    def visit(node: str) -> None:
        state[node] = 1
        stack.append(node)
        for dependency in graph[node]:
            if dependency not in graph:
                continue
            if state.get(dependency, 0) == 0:
                visit(dependency)
            elif state.get(dependency) == 1:
                start = stack.index(dependency)
                cycle = tuple(stack[start:] + [dependency])
                if cycle not in reported:
                    errors.append(f"dependency cycle: {' -> '.join(cycle)}")
                    reported.add(cycle)
        stack.pop()
        state[node] = 2

    for package_name in sorted(graph):
        if state.get(package_name, 0) == 0:
            visit(package_name)


def inspect_repository(root: Path) -> tuple[dict[str, Any], list[str]]:
    root = root.resolve()
    errors: list[str] = []
    records: list[dict[str, Any]] = []
    packages_by_name: dict[str, dict[str, Any]] = {}
    target_owners: dict[str, str] = {}

    manifest_paths = _discover_manifests(root)
    if not manifest_paths:
        errors.append(f"{root.as_posix()}: no {MANIFEST_NAME} files found")

    for manifest_path in manifest_paths:
        relative_path = manifest_path.relative_to(root)
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            _error(errors, relative_path, f"cannot read JSON: {exc}")
            continue

        if not isinstance(manifest, dict):
            _error(errors, relative_path, "manifest root must be an object")
            continue

        if manifest.get("schemaVersion") != 1:
            _error(errors, relative_path, "schemaVersion must be 1")
        if manifest.get("packageKind") != "source-boundary":
            _error(errors, relative_path, "packageKind must be source-boundary in schema v1")

        source_role = manifest.get("sourceRole")
        if not isinstance(source_role, str) or source_role not in ALLOWED_SOURCE_ROLES:
            _error(errors, relative_path, f"sourceRole must be one of {sorted(ALLOWED_SOURCE_ROLES)}")

        owner_domain = manifest.get("ownerDomain")
        if not isinstance(owner_domain, str) or owner_domain not in ALLOWED_OWNER_DOMAINS:
            _error(errors, relative_path, f"ownerDomain must be one of {sorted(ALLOWED_OWNER_DOMAINS)}")

        planned_root = manifest.get("plannedOwnershipRoot")
        if not isinstance(planned_root, str) or not planned_root:
            _error(errors, relative_path, "plannedOwnershipRoot must be a non-empty string")

        if manifest.get("selectable") is not False:
            _error(errors, relative_path, "source boundaries must set selectable to false")
        if manifest.get("catalogVisible") is not False:
            _error(errors, relative_path, "source boundaries must set catalogVisible to false")

        package_name = manifest.get("name")
        if not isinstance(package_name, str) or not package_name:
            _error(errors, relative_path, "name must be a non-empty string")
            package_name = f"<invalid:{relative_path.as_posix()}>"
        elif package_name in packages_by_name:
            previous = packages_by_name[package_name]["path"]
            _error(errors, relative_path, f"duplicate package identity also declared by {previous}")

        dependencies = _string_list(manifest, "dependencies", relative_path, errors)
        targets = _string_list(manifest, "targets", relative_path, errors)
        test_targets = _string_list(manifest, "testTargets", relative_path, errors)
        declared_targets = targets + test_targets
        if len(declared_targets) != len(set(declared_targets)):
            _error(errors, relative_path, "targets and testTargets overlap")

        target_roles = manifest.get("targetRoles")
        if not isinstance(target_roles, dict):
            _error(errors, relative_path, "targetRoles must be an object")
            target_roles = {}
        role_keys = set(target_roles)
        declared_target_set = set(declared_targets)
        if role_keys != declared_target_set:
            missing = sorted(declared_target_set - role_keys)
            extra = sorted(role_keys - declared_target_set)
            if missing:
                _error(errors, relative_path, f"targetRoles missing {missing}")
            if extra:
                _error(errors, relative_path, f"targetRoles contains undeclared targets {extra}")
        for target, role in target_roles.items():
            if not isinstance(role, str) or role not in ALLOWED_TARGET_ROLES:
                _error(errors, relative_path, f"target role for {target} must be one of {sorted(ALLOWED_TARGET_ROLES)}")
            if target in test_targets and role != "test":
                _error(errors, relative_path, f"test target {target} must use role test")
            if target in targets and role == "test":
                _error(errors, relative_path, f"non-test target {target} cannot use role test")

        target_dependencies = manifest.get("targetDependencies")
        if not isinstance(target_dependencies, dict):
            _error(errors, relative_path, "targetDependencies must be an object")
            target_dependencies = {}
        dependency_keys = set(target_dependencies)
        if dependency_keys != declared_target_set:
            missing = sorted(declared_target_set - dependency_keys)
            extra = sorted(dependency_keys - declared_target_set)
            if missing:
                _error(errors, relative_path, f"targetDependencies missing {missing}")
            if extra:
                _error(errors, relative_path, f"targetDependencies contains undeclared targets {extra}")
        for target, values in target_dependencies.items():
            if not isinstance(values, list) or any(not isinstance(item, str) or not item for item in values):
                _error(errors, relative_path, f"targetDependencies.{target} must be an array of non-empty strings")
            elif len(values) != len(set(values)):
                _error(errors, relative_path, f"targetDependencies.{target} contains duplicate entries")

        try:
            cmake_targets = _declared_cmake_targets(manifest_path.parent)
        except (OSError, UnicodeError) as exc:
            _error(errors, relative_path, f"cannot inspect package CMake files: {exc}")
            cmake_targets = set()
        undeclared_cmake_targets = sorted(cmake_targets - declared_target_set)
        if undeclared_cmake_targets:
            _error(errors, relative_path, f"direct CMake targets are not declared: {undeclared_cmake_targets}")

        for target in declared_targets:
            previous_owner = target_owners.get(target)
            if previous_owner is not None:
                _error(errors, relative_path, f"target {target} is already owned by {previous_owner}")
            else:
                target_owners[target] = package_name

        record = {
            "path": relative_path.as_posix(),
            "name": package_name,
            "packageKind": manifest.get("packageKind"),
            "sourceRole": source_role if isinstance(source_role, str) else "<invalid>",
            "ownerDomain": owner_domain if isinstance(owner_domain, str) else "<invalid>",
            "plannedOwnershipRoot": planned_root if isinstance(planned_root, str) else "<invalid>",
            "selectable": manifest.get("selectable"),
            "catalogVisible": manifest.get("catalogVisible"),
            "dependencies": sorted(dependencies),
            "targets": [
                {
                    "name": target,
                    "role": target_roles.get(target),
                    "test": target in test_targets,
                }
                for target in sorted(declared_targets)
            ],
        }
        records.append(record)
        packages_by_name[package_name] = record

    package_names = set(packages_by_name)
    graph: dict[str, list[str]] = {}
    for record in records:
        package_name = record["name"]
        graph[package_name] = record["dependencies"]
        for dependency in record["dependencies"]:
            if dependency == package_name:
                errors.append(f"{record['path']}: package cannot depend on itself")
            elif dependency not in package_names:
                errors.append(f"{record['path']}: unknown package dependency {dependency}")
    _validate_cycle(graph, errors)

    records.sort(key=lambda record: record["path"])
    owner_counts = Counter(record["ownerDomain"] for record in records)
    planned_root_counts = Counter(record["plannedOwnershipRoot"] for record in records)
    inventory = {
        "schemaVersion": 1,
        "summary": {
            "packageCount": len(records),
            "targetCount": sum(len(record["targets"]) for record in records),
            "ownerDomains": dict(sorted(owner_counts.items())),
            "plannedOwnershipRoots": dict(sorted(planned_root_counts.items())),
        },
        "packages": records,
    }
    return inventory, sorted(errors)


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    default_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=default_root, help="repository root")
    parser.add_argument("--output", type=Path, help="write the validated JSON inventory")
    parser.add_argument("--json", action="store_true", help="print the inventory to stdout")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    inventory, errors = inspect_repository(args.root)
    if errors:
        print(f"Package topology validation failed with {len(errors)} error(s):", file=sys.stderr)
        for item in errors:
            print(f"  - {item}", file=sys.stderr)
        return 1

    rendered = json.dumps(inventory, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.output:
        output_path = args.output if args.output.is_absolute() else args.root / args.output
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(rendered, encoding="utf-8", newline="\n")
    if args.json:
        print(rendered, end="")
    else:
        summary = inventory["summary"]
        suffix = f"; inventory={args.output}" if args.output else ""
        print(
            "Package topology OK: "
            f"packages={summary['packageCount']} targets={summary['targetCount']}"
            f" ownership-roots={len(summary['plannedOwnershipRoots'])}{suffix}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
