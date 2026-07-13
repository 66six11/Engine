#!/usr/bin/env python3
"""Validate portable installable package manifests against the v2 contract."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

try:
    from jsonschema import Draft202012Validator
    from jsonschema.exceptions import SchemaError
except ImportError as exc:  # pragma: no cover - exercised only in an unbootstrapped environment.
    raise SystemExit(
        "Package contract validation requires tools/requirements.txt; "
        "run 'python -m pip install -r tools/requirements.txt'."
    ) from exc


MANIFEST_NAME = "asharia.package.json"
IGNORED_PATH_PARTS = {".git", "build", "generated"}
REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SCHEMA_PATH = REPOSITORY_ROOT / "schemas/package-runtime/installable-package-v2.schema.json"
ANY_PLATFORM_ID = "com.asharia.platform.any"
SEMANTIC_VERSION_PATTERN = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-((?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)
ENTRY_MODULE_ROLES = {
    "runtime": {"runtime"},
    "authoring": {"editor"},
    "cook": {"cook"},
    "diagnostics": {"diagnostics"},
}
MODULE_SHIPPING_CLASSES = {
    "contract": {"runtime", "development-only"},
    "runtime": {"runtime"},
    "implementation": {"runtime"},
    "editor": {"editor"},
    "tool": {"tool"},
    "cook": {"tool"},
    "diagnostics": {"runtime", "editor", "tool", "development-only"},
    "content": {"runtime"},
}
SHIPPING_DEPENDENCY_CLASSES = {
    "runtime": {"runtime"},
    "editor": {"runtime", "editor"},
    "tool": {"runtime", "tool"},
    "development-only": {"runtime", "editor", "tool", "development-only"},
}
COLLECTION_ID_NAMES = {
    "dependencies": "dependency",
    "options": "option",
    "modules": "module",
    "contentRoots": "content-root",
    "contributions": "contribution",
}


@dataclass(frozen=True)
class Diagnostic:
    """A deterministic package-contract validation failure."""

    code: str
    manifest_path: str
    pointer: str
    message: str

    def render(self) -> str:
        location = f"{self.manifest_path}{self.pointer}" if self.pointer else self.manifest_path
        return f"{location}: [{self.code}] {self.message}"


@dataclass(frozen=True)
class SemanticVersion:
    major: int
    minor: int
    patch: int
    prerelease: tuple[int | str, ...]


def _json_pointer(parts: Iterable[Any]) -> str:
    encoded = []
    for part in parts:
        value = str(part).replace("~", "~0").replace("/", "~1")
        encoded.append(value)
    return "/" + "/".join(encoded) if encoded else ""


def _diagnostic_sort_key(diagnostic: Diagnostic) -> tuple[str, str, str, str]:
    return (
        diagnostic.manifest_path,
        diagnostic.pointer,
        diagnostic.code,
        diagnostic.message,
    )


def _parse_semantic_version(value: str) -> SemanticVersion:
    match = SEMANTIC_VERSION_PATTERN.fullmatch(value)
    if match is None:
        raise ValueError(f"invalid Semantic Version: {value}")
    prerelease_text = match.group(4)
    prerelease: tuple[int | str, ...] = ()
    if prerelease_text is not None:
        prerelease = tuple(
            int(identifier) if identifier.isdigit() else identifier
            for identifier in prerelease_text.split(".")
        )
    return SemanticVersion(int(match.group(1)), int(match.group(2)), int(match.group(3)), prerelease)


def _compare_semantic_versions(left: str, right: str) -> int:
    lhs = _parse_semantic_version(left)
    rhs = _parse_semantic_version(right)
    lhs_core = (lhs.major, lhs.minor, lhs.patch)
    rhs_core = (rhs.major, rhs.minor, rhs.patch)
    if lhs_core != rhs_core:
        return -1 if lhs_core < rhs_core else 1
    if not lhs.prerelease and not rhs.prerelease:
        return 0
    if not lhs.prerelease:
        return 1
    if not rhs.prerelease:
        return -1
    for lhs_identifier, rhs_identifier in zip(lhs.prerelease, rhs.prerelease):
        if lhs_identifier == rhs_identifier:
            continue
        if isinstance(lhs_identifier, int) and isinstance(rhs_identifier, str):
            return -1
        if isinstance(lhs_identifier, str) and isinstance(rhs_identifier, int):
            return 1
        return -1 if lhs_identifier < rhs_identifier else 1
    if len(lhs.prerelease) == len(rhs.prerelease):
        return 0
    return -1 if len(lhs.prerelease) < len(rhs.prerelease) else 1


def load_schema_validator(schema_path: Path = DEFAULT_SCHEMA_PATH) -> Draft202012Validator:
    """Load and meta-validate the checked-in package schema."""

    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def _schema_diagnostics(
    manifest: Any,
    manifest_path: str,
    validator: Draft202012Validator,
) -> list[Diagnostic]:
    diagnostics = [
        Diagnostic(
            code="package.manifest.schema",
            manifest_path=manifest_path,
            pointer=_json_pointer(error.absolute_path),
            message=error.message,
        )
        for error in validator.iter_errors(manifest)
    ]
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _check_unique_ids(
    values: list[dict[str, Any]],
    collection: str,
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> None:
    first_indices: dict[str, int] = {}
    for index, value in enumerate(values):
        identity = value["id"]
        if identity in first_indices:
            diagnostics.append(
                Diagnostic(
                    code=f"package.{COLLECTION_ID_NAMES[collection]}.duplicate-id",
                    manifest_path=manifest_path,
                    pointer=f"/{collection}/{index}/id",
                    message=f"duplicate id '{identity}'; first declared at index {first_indices[identity]}",
                )
            )
        else:
            first_indices[identity] = index


def _check_version_constraint(
    constraint: dict[str, Any],
    pointer: str,
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> None:
    if constraint["kind"] != "range":
        return
    minimum = constraint["minimumInclusive"]
    maximum = constraint["maximumExclusive"]
    if _compare_semantic_versions(minimum, maximum) >= 0:
        diagnostics.append(
            Diagnostic(
                code="package.version.invalid-range",
                manifest_path=manifest_path,
                pointer=pointer,
                message=f"minimumInclusive '{minimum}' must be lower than maximumExclusive '{maximum}'",
            )
        )


def _check_options(
    options: list[dict[str, Any]],
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> None:
    for index, option in enumerate(options):
        pointer = f"/options/{index}"
        option_type = option["type"]
        default = option["default"]
        if option_type == "enum" and default not in option["values"]:
            diagnostics.append(
                Diagnostic(
                    code="package.option.default-not-allowed",
                    manifest_path=manifest_path,
                    pointer=f"{pointer}/default",
                    message=f"default '{default}' is not present in values",
                )
            )
        elif option_type == "integer":
            minimum = option.get("minimum")
            maximum = option.get("maximum")
            if minimum is not None and maximum is not None and minimum > maximum:
                diagnostics.append(
                    Diagnostic(
                        code="package.option.invalid-range",
                        manifest_path=manifest_path,
                        pointer=pointer,
                        message=f"minimum {minimum} must not exceed maximum {maximum}",
                    )
                )
            if minimum is not None and default < minimum:
                diagnostics.append(
                    Diagnostic(
                        code="package.option.default-out-of-range",
                        manifest_path=manifest_path,
                        pointer=f"{pointer}/default",
                        message=f"default {default} is lower than minimum {minimum}",
                    )
                )
            if maximum is not None and default > maximum:
                diagnostics.append(
                    Diagnostic(
                        code="package.option.default-out-of-range",
                        manifest_path=manifest_path,
                        pointer=f"{pointer}/default",
                        message=f"default {default} exceeds maximum {maximum}",
                    )
                )
        elif option_type == "string":
            minimum = option.get("minimumLength")
            maximum = option.get("maximumLength")
            if minimum is not None and maximum is not None and minimum > maximum:
                diagnostics.append(
                    Diagnostic(
                        code="package.option.invalid-length-range",
                        manifest_path=manifest_path,
                        pointer=pointer,
                        message=f"minimumLength {minimum} must not exceed maximumLength {maximum}",
                    )
                )
            if minimum is not None and len(default) < minimum:
                diagnostics.append(
                    Diagnostic(
                        code="package.option.default-length-out-of-range",
                        manifest_path=manifest_path,
                        pointer=f"{pointer}/default",
                        message=f"default length {len(default)} is lower than minimumLength {minimum}",
                    )
                )
            if maximum is not None and len(default) > maximum:
                diagnostics.append(
                    Diagnostic(
                        code="package.option.default-length-out-of-range",
                        manifest_path=manifest_path,
                        pointer=f"{pointer}/default",
                        message=f"default length {len(default)} exceeds maximumLength {maximum}",
                    )
                )


def _check_platform_list(
    platforms: list[str],
    pointer: str,
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> None:
    if ANY_PLATFORM_ID in platforms and len(platforms) != 1:
        diagnostics.append(
            Diagnostic(
                code="package.platform.invalid-any-combination",
                manifest_path=manifest_path,
                pointer=pointer,
                message=f"'{ANY_PLATFORM_ID}' cannot be combined with specific platform IDs",
            )
        )


def _check_module_graph(
    modules: list[dict[str, Any]],
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> dict[str, dict[str, Any]]:
    module_indices: dict[str, int] = {}
    modules_by_id: dict[str, dict[str, Any]] = {}
    dependency_indices: dict[tuple[str, str], int] = {}
    for index, module in enumerate(modules):
        module_indices.setdefault(module["id"], index)
        modules_by_id.setdefault(module["id"], module)

    for module_index, module in enumerate(modules):
        identity = module["id"]
        _check_platform_list(
            module["platforms"],
            f"/modules/{module_index}/platforms",
            manifest_path,
            diagnostics,
        )
        allowed_shipping = MODULE_SHIPPING_CLASSES[module["role"]]
        if module["shippingClass"] not in allowed_shipping:
            diagnostics.append(
                Diagnostic(
                    code="package.module.invalid-shipping-class",
                    manifest_path=manifest_path,
                    pointer=f"/modules/{module_index}/shippingClass",
                    message=(
                        f"role '{module['role']}' does not allow shipping class "
                        f"'{module['shippingClass']}'"
                    ),
                )
            )
        for dependency_index, dependency in enumerate(module["dependsOn"]):
            dependency_indices[(identity, dependency)] = dependency_index
            pointer = f"/modules/{module_index}/dependsOn/{dependency_index}"
            if dependency == identity:
                diagnostics.append(
                    Diagnostic(
                        code="package.module.self-dependency",
                        manifest_path=manifest_path,
                        pointer=pointer,
                        message=f"module '{identity}' cannot depend on itself",
                    )
                )
            elif dependency not in modules_by_id:
                diagnostics.append(
                    Diagnostic(
                        code="package.module.unknown-dependency",
                        manifest_path=manifest_path,
                        pointer=pointer,
                        message=f"unknown local module '{dependency}'",
                    )
                )
            else:
                dependency_module = modules_by_id[dependency]
                allowed_dependency_shipping = SHIPPING_DEPENDENCY_CLASSES[module["shippingClass"]]
                if dependency_module["shippingClass"] not in allowed_dependency_shipping:
                    diagnostics.append(
                        Diagnostic(
                            code="package.module.shipping-closure",
                            manifest_path=manifest_path,
                            pointer=pointer,
                            message=(
                                f"module '{identity}' with shipping class '{module['shippingClass']}' "
                                f"cannot depend on '{dependency}' with shipping class "
                                f"'{dependency_module['shippingClass']}'"
                            ),
                        )
                    )
                missing_hosts = sorted(set(module["hostKinds"]) - set(dependency_module["hostKinds"]))
                if missing_hosts:
                    diagnostics.append(
                        Diagnostic(
                            code="package.module.host-closure",
                            manifest_path=manifest_path,
                            pointer=pointer,
                            message=(
                                f"dependency module '{dependency}' is unavailable for required hosts "
                                f"{missing_hosts}"
                            ),
                        )
                    )
                dependency_platforms = set(dependency_module["platforms"])
                module_platforms = set(module["platforms"])
                if ANY_PLATFORM_ID not in dependency_platforms:
                    missing_platforms = (
                        [ANY_PLATFORM_ID]
                        if ANY_PLATFORM_ID in module_platforms
                        else sorted(module_platforms - dependency_platforms)
                    )
                    if missing_platforms:
                        diagnostics.append(
                            Diagnostic(
                                code="package.module.platform-closure",
                                manifest_path=manifest_path,
                                pointer=pointer,
                                message=(
                                    f"dependency module '{dependency}' is unavailable for required platforms "
                                    f"{missing_platforms}"
                                ),
                            )
                        )

    state: dict[str, int] = {}
    stack: list[str] = []
    reported_edges: set[tuple[str, str]] = set()

    def visit(identity: str) -> None:
        state[identity] = 1
        stack.append(identity)
        for dependency in modules_by_id[identity]["dependsOn"]:
            if dependency not in modules_by_id or dependency == identity:
                continue
            if state.get(dependency, 0) == 0:
                visit(dependency)
            elif state.get(dependency) == 1 and (identity, dependency) not in reported_edges:
                cycle_start = stack.index(dependency)
                cycle = stack[cycle_start:] + [dependency]
                dependency_index = dependency_indices[(identity, dependency)]
                diagnostics.append(
                    Diagnostic(
                        code="package.module.cycle",
                        manifest_path=manifest_path,
                        pointer=f"/modules/{module_indices[identity]}/dependsOn/{dependency_index}",
                        message=f"module dependency cycle: {' -> '.join(cycle)}",
                    )
                )
                reported_edges.add((identity, dependency))
        stack.pop()
        state[identity] = 2

    for identity in modules_by_id:
        if state.get(identity, 0) == 0:
            visit(identity)
    return modules_by_id


def _check_entry_modules(
    entries: dict[str, list[str]],
    modules_by_id: dict[str, dict[str, Any]],
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> None:
    for dimension, references in entries.items():
        allowed_roles = ENTRY_MODULE_ROLES[dimension]
        for index, reference in enumerate(references):
            pointer = f"/entryModules/{dimension}/{index}"
            module = modules_by_id.get(reference)
            if module is None:
                diagnostics.append(
                    Diagnostic(
                        code="package.entry.unknown-module",
                        manifest_path=manifest_path,
                        pointer=pointer,
                        message=f"unknown local module '{reference}'",
                    )
                )
            elif module["role"] not in allowed_roles:
                diagnostics.append(
                    Diagnostic(
                        code="package.entry.role-mismatch",
                        manifest_path=manifest_path,
                        pointer=pointer,
                        message=(
                            f"entry dimension '{dimension}' cannot reference module '{reference}' "
                            f"with role '{module['role']}'"
                        ),
                    )
                )


def _check_content_roots(
    content_roots: list[dict[str, Any]],
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> None:
    for index, content_root in enumerate(content_roots):
        _check_platform_list(
            content_root["platforms"],
            f"/contentRoots/{index}/platforms",
            manifest_path,
            diagnostics,
        )
        value = content_root["path"]
        segments = value.split("/")
        if any(segment in {"", ".", ".."} for segment in segments) or ":" in value:
            diagnostics.append(
                Diagnostic(
                    code="package.content-root.invalid-path",
                    manifest_path=manifest_path,
                    pointer=f"/contentRoots/{index}/path",
                    message=f"'{value}' must be a normalized package-relative path",
                )
            )


def _check_contributions(
    contributions: list[dict[str, Any]],
    modules_by_id: dict[str, dict[str, Any]],
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> None:
    for index, contribution in enumerate(contributions):
        pointer = f"/contributions/{index}"
        module = modules_by_id.get(contribution["module"])
        if module is None:
            diagnostics.append(
                Diagnostic(
                    code="package.contribution.unknown-module",
                    manifest_path=manifest_path,
                    pointer=f"{pointer}/module",
                    message=f"unknown owner module '{contribution['module']}'",
                )
            )
            continue
        if contribution["shippingClass"] != module["shippingClass"]:
            diagnostics.append(
                Diagnostic(
                    code="package.contribution.shipping-mismatch",
                    manifest_path=manifest_path,
                    pointer=f"{pointer}/shippingClass",
                    message=(
                        f"contribution shipping class '{contribution['shippingClass']}' must match "
                        f"owner module '{module['id']}' shipping class '{module['shippingClass']}'"
                    ),
                )
            )
        unsupported_hosts = sorted(set(contribution["hostKinds"]) - set(module["hostKinds"]))
        if unsupported_hosts:
            diagnostics.append(
                Diagnostic(
                    code="package.contribution.host-mismatch",
                    manifest_path=manifest_path,
                    pointer=f"{pointer}/hostKinds",
                    message=f"owner module '{module['id']}' does not support hosts {unsupported_hosts}",
                )
            )


def _check_catalog_policy(
    manifest: dict[str, Any],
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> None:
    catalog_type = manifest["catalogType"]
    entries = manifest["entryModules"]
    content_roots = manifest["contentRoots"]
    if not manifest["modules"] and not content_roots:
        diagnostics.append(
            Diagnostic(
                code="package.catalog.empty",
                manifest_path=manifest_path,
                pointer="",
                message="an installable package must provide at least one logical module or content root",
            )
        )
    if catalog_type == "system" and "runtime" not in entries:
        diagnostics.append(
            Diagnostic(
                code="package.catalog.system-missing-runtime",
                manifest_path=manifest_path,
                pointer="/entryModules",
                message="a system package must provide a runtime entry",
            )
        )
    elif catalog_type == "feature" and "runtime" not in entries and not content_roots:
        diagnostics.append(
            Diagnostic(
                code="package.catalog.feature-missing-capability",
                manifest_path=manifest_path,
                pointer="",
                message="a feature package must provide a runtime entry or content root",
            )
        )
    elif catalog_type == "integration":
        if len({dependency["id"] for dependency in manifest["dependencies"]}) < 2:
            diagnostics.append(
                Diagnostic(
                    code="package.catalog.integration-missing-dependencies",
                    manifest_path=manifest_path,
                    pointer="/dependencies",
                    message="an integration package must depend on at least two complete packages",
                )
            )
        if not manifest["contributions"]:
            diagnostics.append(
                Diagnostic(
                    code="package.catalog.integration-missing-contribution",
                    manifest_path=manifest_path,
                    pointer="/contributions",
                    message="an integration package must declare at least one contribution",
                )
            )
    elif catalog_type == "content" and not content_roots:
        diagnostics.append(
            Diagnostic(
                code="package.catalog.content-missing-root",
                manifest_path=manifest_path,
                pointer="/contentRoots",
                message="a content package must provide at least one content root",
            )
        )
    elif catalog_type == "template":
        if not any(content_root["role"] == "template" for content_root in content_roots):
            diagnostics.append(
                Diagnostic(
                    code="package.catalog.template-missing-root",
                    manifest_path=manifest_path,
                    pointer="/contentRoots",
                    message="a template package must provide at least one template content root",
                )
            )
        if "runtime" in entries:
            diagnostics.append(
                Diagnostic(
                    code="package.catalog.template-runtime-entry",
                    manifest_path=manifest_path,
                    pointer="/entryModules/runtime",
                    message="a template package cannot declare a runtime entry",
                )
            )


def _semantic_diagnostics(manifest: dict[str, Any], manifest_path: str) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    for collection in ("dependencies", "options", "modules", "contentRoots", "contributions"):
        _check_unique_ids(manifest[collection], collection, manifest_path, diagnostics)

    package_id = manifest["id"]
    for index, dependency in enumerate(manifest["dependencies"]):
        if dependency["id"] == package_id:
            diagnostics.append(
                Diagnostic(
                    code="package.dependency.self",
                    manifest_path=manifest_path,
                    pointer=f"/dependencies/{index}/id",
                    message="a package cannot depend on itself",
                )
            )
        _check_version_constraint(
            dependency["version"],
            f"/dependencies/{index}/version",
            manifest_path,
            diagnostics,
        )
    _check_version_constraint(manifest["engineApi"], "/engineApi", manifest_path, diagnostics)
    _check_options(manifest["options"], manifest_path, diagnostics)
    modules_by_id = _check_module_graph(manifest["modules"], manifest_path, diagnostics)
    _check_entry_modules(
        manifest["entryModules"],
        modules_by_id,
        manifest_path,
        diagnostics,
    )
    _check_content_roots(manifest["contentRoots"], manifest_path, diagnostics)
    _check_contributions(manifest["contributions"], modules_by_id, manifest_path, diagnostics)
    _check_catalog_policy(manifest, manifest_path, diagnostics)
    return sorted(diagnostics, key=_diagnostic_sort_key)


def validate_manifest_data(
    manifest: Any,
    manifest_path: str,
    validator: Draft202012Validator,
) -> list[Diagnostic]:
    """Validate already-parsed JSON and return stable, sorted diagnostics."""

    schema_diagnostics = _schema_diagnostics(manifest, manifest_path, validator)
    if schema_diagnostics:
        return schema_diagnostics
    return _semantic_diagnostics(manifest, manifest_path)


def validate_manifest_file(path: Path, validator: Draft202012Validator) -> list[Diagnostic]:
    """Read and validate one explicit v2 manifest file."""

    manifest_path = path.as_posix()
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return [
            Diagnostic(
                code="package.manifest.json",
                manifest_path=manifest_path,
                pointer="",
                message=f"cannot read JSON: {exc}",
            )
        ]
    return validate_manifest_data(manifest, manifest_path, validator)


def discover_installable_manifests(root: Path) -> list[Path]:
    """Route exact v1 manifests to topology validation and all other manifests here."""

    paths: list[Path] = []
    for path in root.rglob(MANIFEST_NAME):
        relative = path.relative_to(root)
        if any(part in IGNORED_PATH_PARTS for part in relative.parts):
            continue
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError):
            paths.append(path)
            continue
        if not isinstance(manifest, dict):
            paths.append(path)
            continue
        if manifest.get("schemaVersion") == 1 and manifest.get("packageKind") == "source-boundary":
            continue
        paths.append(path)
    return sorted(paths, key=lambda item: item.relative_to(root).as_posix())


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifests", nargs="*", type=Path, help="explicit v2 manifests to validate")
    parser.add_argument("--root", type=Path, default=REPOSITORY_ROOT, help="repository root")
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA_PATH, help="v2 JSON Schema path")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    root = args.root.resolve()
    schema_path = args.schema if args.schema.is_absolute() else root / args.schema
    try:
        validator = load_schema_validator(schema_path)
    except (OSError, UnicodeError, json.JSONDecodeError, SchemaError) as exc:
        print(f"Package contract schema is invalid: {exc}", file=sys.stderr)
        return 2

    paths = [path if path.is_absolute() else root / path for path in args.manifests]
    if not paths:
        paths = discover_installable_manifests(root)

    diagnostics: list[Diagnostic] = []
    for path in paths:
        diagnostics.extend(validate_manifest_file(path, validator))
    diagnostics.sort(key=_diagnostic_sort_key)
    if diagnostics:
        print(f"Package contract validation failed with {len(diagnostics)} error(s):", file=sys.stderr)
        for diagnostic in diagnostics:
            print(f"  - {diagnostic.render()}", file=sys.stderr)
        return 1

    print(f"Package contracts OK: installable-manifests={len(paths)} schema=draft-2020-12")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
