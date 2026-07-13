#!/usr/bin/env python3
"""Validate Asharia package-runtime contracts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

try:
    from jsonschema import Draft202012Validator
    from jsonschema.exceptions import SchemaError
    from referencing import Registry, Resource
except ImportError as exc:  # pragma: no cover - exercised only in an unbootstrapped environment.
    raise SystemExit(
        "Package contract validation requires tools/requirements.txt; "
        "run 'python -m pip install -r tools/requirements.txt'."
    ) from exc


PACKAGE_MANIFEST_NAME = "asharia.package.json"
PROJECT_MANIFEST_NAME = "asharia.packages.json"
PACKAGE_LOCK_NAME = "asharia.packages.lock.json"
HOST_PROFILE_NAME = "asharia.host-profile.json"
IGNORED_PATH_PARTS = {".git", "build", "generated"}
REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SCHEMA_ROOT = REPOSITORY_ROOT / "schemas/package-runtime"
COMMON_SCHEMA_NAME = "package-contract-common-v1.schema.json"
INSTALLABLE_SCHEMA_NAME = "installable-package-v2.schema.json"
FEATURE_SET_SCHEMA_NAME = "feature-set-v2.schema.json"
PROJECT_SCHEMA_NAME = "project-package-manifest-v1.schema.json"
LOCK_SCHEMA_NAME = "package-lock-v1.schema.json"
HOST_PROFILE_SCHEMA_NAME = "host-profile-v1.schema.json"
PACKAGE_TREE_HEADER = b"asharia-package-tree-v1\0"
PACKAGE_TREE_ROOT_EXCLUDES = {".git", ".hg", ".svn", "build", "generated"}
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
MODULE_ROLE_ORDER = (
    "contract",
    "runtime",
    "implementation",
    "editor",
    "tool",
    "cook",
    "diagnostics",
    "content",
)
SHIPPING_CLASS_ORDER = ("runtime", "editor", "tool", "development-only")
HOST_PROFILE_POLICIES = {
    "minimal": {
        "requiredRoles": ("contract",),
        "allowedRoles": ("contract",),
        "allowedShippingClasses": ("runtime",),
        "contributionMode": "deny-all",
    },
    "editor": {
        "requiredRoles": (
            "contract",
            "runtime",
            "implementation",
            "editor",
            "diagnostics",
            "content",
        ),
        "allowedRoles": MODULE_ROLE_ORDER,
        "allowedShippingClasses": SHIPPING_CLASS_ORDER,
        "contributionMode": "allow-compatible",
    },
    "runtime": {
        "requiredRoles": ("contract", "runtime", "implementation", "content"),
        "allowedRoles": ("contract", "runtime", "implementation", "diagnostics", "content"),
        "allowedShippingClasses": ("runtime",),
        "contributionMode": "allow-compatible",
    },
    "dedicated-server": {
        "requiredRoles": ("contract", "runtime", "implementation", "content"),
        "allowedRoles": ("contract", "runtime", "implementation", "diagnostics", "content"),
        "allowedShippingClasses": ("runtime",),
        "contributionMode": "allow-compatible",
    },
    "asset-worker": {
        "requiredRoles": ("contract", "tool", "cook", "diagnostics", "content"),
        "allowedRoles": (
            "contract",
            "runtime",
            "implementation",
            "tool",
            "cook",
            "diagnostics",
            "content",
        ),
        "allowedShippingClasses": ("runtime", "tool", "development-only"),
        "contributionMode": "allow-compatible",
    },
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


class PackageTreeIntegrityError(ValueError):
    """A stable package-tree contract failure with a payload-relative location."""

    def __init__(self, code: str, relative_path: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.relative_path = relative_path


@dataclass(frozen=True)
class SemanticVersion:
    major: int
    minor: int
    patch: int
    prerelease: tuple[int | str, ...]


@dataclass(frozen=True)
class ContractValidators:
    """Preloaded validators sharing one offline schema registry."""

    installable: Draft202012Validator
    feature_set: Draft202012Validator
    project: Draft202012Validator
    lock: Draft202012Validator
    host_profile: Draft202012Validator


@dataclass(frozen=True, order=True)
class HostModuleSelection:
    """One logical package module selected for a Host Profile."""

    package_id: str
    package_version: str
    module_id: str


@dataclass(frozen=True, order=True)
class HostContributionSelection:
    """One contribution whose owner module is present in the Host selection."""

    package_id: str
    package_version: str
    contribution_id: str
    contribution_kind: str
    owner_module_id: str


@dataclass(frozen=True)
class HostProjection:
    """Deterministic logical selection; deliberately not an Activation Plan."""

    host_kind: str
    target_platform: str
    modules: tuple[HostModuleSelection, ...]
    contributions: tuple[HostContributionSelection, ...]


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


def compare_semantic_versions(left: str, right: str) -> int:
    """Compare two schema-valid Semantic Versions using SemVer precedence."""

    return _compare_semantic_versions(left, right)


def _load_schema(path: Path) -> dict[str, Any]:
    schema = json.loads(path.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return schema


def load_contract_validators(schema_root: Path = DEFAULT_SCHEMA_ROOT) -> ContractValidators:
    """Load all author-contract schemas with offline-only reference resolution."""

    common = _load_schema(schema_root / COMMON_SCHEMA_NAME)
    registry = Registry().with_resource(common["$id"], Resource.from_contents(common))

    def create(schema_name: str) -> Draft202012Validator:
        schema = _load_schema(schema_root / schema_name)
        return Draft202012Validator(schema, registry=registry)

    return ContractValidators(
        installable=create(INSTALLABLE_SCHEMA_NAME),
        feature_set=create(FEATURE_SET_SCHEMA_NAME),
        project=create(PROJECT_SCHEMA_NAME),
        lock=create(LOCK_SCHEMA_NAME),
        host_profile=create(HOST_PROFILE_SCHEMA_NAME),
    )


def load_schema_validator(
    schema_path: Path = DEFAULT_SCHEMA_ROOT / INSTALLABLE_SCHEMA_NAME,
) -> Draft202012Validator:
    """Load one schema with shared definitions; retained for focused callers."""

    common = _load_schema(schema_path.parent / COMMON_SCHEMA_NAME)
    registry = Registry().with_resource(common["$id"], Resource.from_contents(common))
    return Draft202012Validator(_load_schema(schema_path), registry=registry)


def _schema_diagnostics(
    manifest: Any,
    manifest_path: str,
    validator: Draft202012Validator,
    code: str = "package.manifest.schema",
) -> list[Diagnostic]:
    diagnostics = [
        Diagnostic(
            code=code,
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
    code: str = "package.version.invalid-range",
) -> None:
    if constraint["kind"] != "range":
        return
    minimum = constraint["minimumInclusive"]
    maximum = constraint["maximumExclusive"]
    if _compare_semantic_versions(minimum, maximum) >= 0:
        diagnostics.append(
            Diagnostic(
                code=code,
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


def _installable_semantic_diagnostics(
    manifest: dict[str, Any], manifest_path: str
) -> list[Diagnostic]:
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


def _check_requirement_collection(
    requirements: list[dict[str, Any]],
    collection: str,
    code_prefix: str,
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> dict[str, int]:
    first_indices: dict[str, int] = {}
    for index, requirement in enumerate(requirements):
        identity = requirement["id"]
        if identity in first_indices:
            diagnostics.append(
                Diagnostic(
                    code=f"{code_prefix}.duplicate-id",
                    manifest_path=manifest_path,
                    pointer=f"/{collection}/{index}/id",
                    message=(
                        f"duplicate id '{identity}'; first declared at index "
                        f"{first_indices[identity]}"
                    ),
                )
            )
        else:
            first_indices[identity] = index
        _check_version_constraint(
            requirement["version"],
            f"/{collection}/{index}/version",
            manifest_path,
            diagnostics,
            code=f"{code_prefix}.invalid-range",
        )
    return first_indices


def _project_semantic_diagnostics(
    manifest: dict[str, Any], manifest_path: str
) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    package_indices = _check_requirement_collection(
        manifest["directPackages"],
        "directPackages",
        "project.package",
        manifest_path,
        diagnostics,
    )
    feature_set_indices = _check_requirement_collection(
        manifest["directFeatureSets"],
        "directFeatureSets",
        "project.feature-set",
        manifest_path,
        diagnostics,
    )
    for identity in sorted(package_indices.keys() & feature_set_indices.keys()):
        diagnostics.append(
            Diagnostic(
                code="project.selection.cross-kind-id",
                manifest_path=manifest_path,
                pointer=f"/directFeatureSets/{feature_set_indices[identity]}/id",
                message=(
                    f"identity '{identity}' is already selected as a direct package at index "
                    f"{package_indices[identity]}"
                ),
            )
        )

    option_package_indices: dict[str, int] = {}
    for package_index, package_options in enumerate(manifest["packageOptions"]):
        package_id = package_options["packageId"]
        if package_id in option_package_indices:
            diagnostics.append(
                Diagnostic(
                    code="project.option.duplicate-package-id",
                    manifest_path=manifest_path,
                    pointer=f"/packageOptions/{package_index}/packageId",
                    message=(
                        f"duplicate package option group '{package_id}'; first declared at index "
                        f"{option_package_indices[package_id]}"
                    ),
                )
            )
        else:
            option_package_indices[package_id] = package_index

        option_indices: dict[str, int] = {}
        for option_index, option in enumerate(package_options["values"]):
            option_id = option["id"]
            if option_id in option_indices:
                diagnostics.append(
                    Diagnostic(
                        code="project.option.duplicate-id",
                        manifest_path=manifest_path,
                        pointer=f"/packageOptions/{package_index}/values/{option_index}/id",
                        message=(
                            f"duplicate option id '{option_id}'; first declared at index "
                            f"{option_indices[option_id]}"
                        ),
                    )
                )
            else:
                option_indices[option_id] = option_index
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _feature_set_semantic_diagnostics(
    manifest: dict[str, Any], manifest_path: str
) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    package_indices = _check_requirement_collection(
        manifest["packages"],
        "packages",
        "feature-set.package",
        manifest_path,
        diagnostics,
    )
    feature_set_indices = _check_requirement_collection(
        manifest["featureSets"],
        "featureSets",
        "feature-set.feature-set",
        manifest_path,
        diagnostics,
    )
    for identity in sorted(package_indices.keys() & feature_set_indices.keys()):
        diagnostics.append(
            Diagnostic(
                code="feature-set.member.cross-kind-id",
                manifest_path=manifest_path,
                pointer=f"/featureSets/{feature_set_indices[identity]}/id",
                message=(
                    f"identity '{identity}' is already declared as a package member at index "
                    f"{package_indices[identity]}"
                ),
            )
        )

    feature_set_id = manifest["id"]
    for collection, indices in (
        ("packages", package_indices),
        ("featureSets", feature_set_indices),
    ):
        if feature_set_id in indices:
            diagnostics.append(
                Diagnostic(
                    code="feature-set.member.self",
                    manifest_path=manifest_path,
                    pointer=f"/{collection}/{indices[feature_set_id]}/id",
                    message=f"Feature Set '{feature_set_id}' cannot contain itself",
                )
            )

    if not manifest["packages"] and not manifest["featureSets"]:
        diagnostics.append(
            Diagnostic(
                code="feature-set.member.empty",
                manifest_path=manifest_path,
                pointer="",
                message="a Feature Set must contain at least one package or nested Feature Set",
            )
        )
    _check_version_constraint(
        manifest["engineApi"],
        "/engineApi",
        manifest_path,
        diagnostics,
        code="feature-set.engine-api.invalid-range",
    )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _is_normalized_relative_path(value: str) -> bool:
    if unicodedata.normalize("NFC", value) != value or "\\" in value or ":" in value:
        return False
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        return False
    segments = value.split("/")
    return bool(value) and not value.startswith("/") and all(
        segment not in {"", ".", ".."} for segment in segments
    )


def _check_lock_reference(
    reference: dict[str, Any],
    pointer: str,
    nodes_by_id: dict[str, dict[str, Any]],
    manifest_path: str,
    diagnostics: list[Diagnostic],
) -> bool:
    identity = reference["id"]
    node = nodes_by_id.get(identity)
    if node is None:
        diagnostics.append(
            Diagnostic(
                code="lock.reference.missing-node",
                manifest_path=manifest_path,
                pointer=pointer,
                message=f"exact reference '{identity}' does not exist in nodes",
            )
        )
        return False
    valid = True
    if reference["version"] != node["version"]:
        diagnostics.append(
            Diagnostic(
                code="lock.reference.version-mismatch",
                manifest_path=manifest_path,
                pointer=f"{pointer}/version",
                message=(
                    f"reference version '{reference['version']}' does not match locked node "
                    f"version '{node['version']}'"
                ),
            )
        )
        valid = False
    if reference["packageKind"] != node["packageKind"]:
        diagnostics.append(
            Diagnostic(
                code="lock.reference.kind-mismatch",
                manifest_path=manifest_path,
                pointer=f"{pointer}/packageKind",
                message=(
                    f"reference kind '{reference['packageKind']}' does not match locked node "
                    f"kind '{node['packageKind']}'"
                ),
            )
        )
        valid = False
    return valid


def _lock_semantic_diagnostics(manifest: dict[str, Any], manifest_path: str) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    nodes_by_id: dict[str, dict[str, Any]] = {}
    node_indices: dict[str, int] = {}
    dependency_indices: dict[tuple[str, str], int] = {}

    for node_index, node in enumerate(manifest["nodes"]):
        identity = node["id"]
        previous = nodes_by_id.get(identity)
        if previous is not None:
            code = (
                "lock.node.duplicate-id"
                if previous["version"] == node["version"]
                else "lock.node.multiple-versions"
            )
            diagnostics.append(
                Diagnostic(
                    code=code,
                    manifest_path=manifest_path,
                    pointer=f"/nodes/{node_index}/id",
                    message=(
                        f"node identity '{identity}' was first declared at index "
                        f"{node_indices[identity]} with version '{previous['version']}'"
                    ),
                )
            )
            continue
        nodes_by_id[identity] = node
        node_indices[identity] = node_index

        source = node["source"]
        relative_path = source.get("relativePath")
        if relative_path is not None and not _is_normalized_relative_path(relative_path):
            diagnostics.append(
                Diagnostic(
                    code="lock.source.invalid-relative-path",
                    manifest_path=manifest_path,
                    pointer=f"/nodes/{node_index}/source/relativePath",
                    message=f"'{relative_path}' must be a normalized portable relative path",
                )
            )

    root_ids: dict[str, tuple[str, int]] = {}
    reachable_roots: set[str] = set()
    for collection, required_kind in (
        ("directPackages", "installable-capability"),
        ("directFeatureSets", "feature-set"),
    ):
        for root_index, reference in enumerate(manifest["roots"][collection]):
            pointer = f"/roots/{collection}/{root_index}"
            identity = reference["id"]
            if identity in root_ids:
                previous_collection, previous_index = root_ids[identity]
                diagnostics.append(
                    Diagnostic(
                        code="lock.root.duplicate-id",
                        manifest_path=manifest_path,
                        pointer=f"{pointer}/id",
                        message=(
                            f"root '{identity}' was first declared at "
                            f"/roots/{previous_collection}/{previous_index}"
                        ),
                    )
                )
            else:
                root_ids[identity] = (collection, root_index)
            if reference["packageKind"] != required_kind:
                diagnostics.append(
                    Diagnostic(
                        code="lock.root.invalid-kind",
                        manifest_path=manifest_path,
                        pointer=f"{pointer}/packageKind",
                        message=f"{collection} requires package kind '{required_kind}'",
                    )
                )
            if _check_lock_reference(
                reference,
                pointer,
                nodes_by_id,
                manifest_path,
                diagnostics,
            ):
                reachable_roots.add(identity)

    for identity, node in nodes_by_id.items():
        node_index = node_indices[identity]
        dependency_ids: dict[str, int] = {}
        for dependency_index, reference in enumerate(node["dependencies"]):
            dependency = reference["id"]
            pointer = f"/nodes/{node_index}/dependencies/{dependency_index}"
            if dependency in dependency_ids:
                diagnostics.append(
                    Diagnostic(
                        code="lock.dependency.duplicate-id",
                        manifest_path=manifest_path,
                        pointer=f"{pointer}/id",
                        message=(
                            f"dependency '{dependency}' was first declared at index "
                            f"{dependency_ids[dependency]}"
                        ),
                    )
                )
            else:
                dependency_ids[dependency] = dependency_index
                dependency_indices[(identity, dependency)] = dependency_index
            if dependency == identity:
                diagnostics.append(
                    Diagnostic(
                        code="lock.dependency.self",
                        manifest_path=manifest_path,
                        pointer=f"{pointer}/id",
                        message=f"node '{identity}' cannot depend on itself",
                    )
                )
                continue
            if node["packageKind"] == "installable-capability" and reference[
                "packageKind"
            ] != "installable-capability":
                diagnostics.append(
                    Diagnostic(
                        code="lock.dependency.invalid-kind",
                        manifest_path=manifest_path,
                        pointer=f"{pointer}/packageKind",
                        message="installable nodes cannot depend on Feature Set nodes",
                    )
                )
            _check_lock_reference(
                reference,
                pointer,
                nodes_by_id,
                manifest_path,
                diagnostics,
            )

    state: dict[str, int] = {}
    stack: list[str] = []
    reported_edges: set[tuple[str, str]] = set()

    def visit(identity: str) -> None:
        state[identity] = 1
        stack.append(identity)
        for reference in nodes_by_id[identity]["dependencies"]:
            dependency = reference["id"]
            if dependency == identity or dependency not in nodes_by_id:
                continue
            if state.get(dependency, 0) == 0:
                visit(dependency)
            elif state.get(dependency) == 1 and (identity, dependency) not in reported_edges:
                cycle_start = stack.index(dependency)
                cycle = stack[cycle_start:] + [dependency]
                dependency_index = dependency_indices.get((identity, dependency), 0)
                diagnostics.append(
                    Diagnostic(
                        code="lock.dependency.cycle",
                        manifest_path=manifest_path,
                        pointer=(
                            f"/nodes/{node_indices[identity]}/dependencies/"
                            f"{dependency_index}/id"
                        ),
                        message=f"locked dependency cycle: {' -> '.join(cycle)}",
                    )
                )
                reported_edges.add((identity, dependency))
        stack.pop()
        state[identity] = 2

    for identity in sorted(nodes_by_id):
        if state.get(identity, 0) == 0:
            visit(identity)

    reachable: set[str] = set()
    pending = list(reachable_roots)
    while pending:
        identity = pending.pop()
        if identity in reachable or identity not in nodes_by_id:
            continue
        reachable.add(identity)
        pending.extend(reference["id"] for reference in nodes_by_id[identity]["dependencies"])
    for identity in sorted(nodes_by_id.keys() - reachable):
        diagnostics.append(
            Diagnostic(
                code="lock.node.unreachable",
                manifest_path=manifest_path,
                pointer=f"/nodes/{node_indices[identity]}/id",
                message=f"node '{identity}' is not reachable from a direct root",
            )
        )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _host_profile_semantic_diagnostics(
    manifest: dict[str, Any], manifest_path: str
) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    policy = HOST_PROFILE_POLICIES[manifest["hostKind"]]
    collection_policies = (
        (
            "requiredRoles",
            "host.profile.required-roles-mismatch",
            "required role",
        ),
        (
            "allowedRoles",
            "host.profile.allowed-roles-mismatch",
            "allowed role",
        ),
        (
            "allowedShippingClasses",
            "host.profile.shipping-policy-mismatch",
            "allowed shipping class",
        ),
    )
    for field, code, label in collection_policies:
        actual = set(manifest[field])
        expected = set(policy[field])
        if actual != expected:
            missing = sorted(expected - actual)
            unexpected = sorted(actual - expected)
            diagnostics.append(
                Diagnostic(
                    code=code,
                    manifest_path=manifest_path,
                    pointer=f"/{field}",
                    message=(
                        f"standard host '{manifest['hostKind']}' {label} policy differs; "
                        f"missing={missing}, unexpected={unexpected}"
                    ),
                )
            )

    contribution_mode = manifest["contributionFilter"]["mode"]
    if contribution_mode != policy["contributionMode"]:
        diagnostics.append(
            Diagnostic(
                code="host.profile.contribution-policy-mismatch",
                manifest_path=manifest_path,
                pointer="/contributionFilter/mode",
                message=(
                    f"standard host '{manifest['hostKind']}' requires contribution mode "
                    f"'{policy['contributionMode']}'"
                ),
            )
        )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _version_satisfies_constraint(version: str, constraint: dict[str, Any]) -> bool:
    if constraint["kind"] == "exact":
        return version == constraint["version"]
    parsed = _parse_semantic_version(version)
    if parsed.prerelease and not constraint["allowPrerelease"]:
        return False
    return (
        _compare_semantic_versions(version, constraint["minimumInclusive"]) >= 0
        and _compare_semantic_versions(version, constraint["maximumExclusive"]) < 0
    )


def version_satisfies_constraint(version: str, constraint: dict[str, Any]) -> bool:
    """Evaluate one schema-valid Semantic Version against a v1 constraint."""

    return _version_satisfies_constraint(version, constraint)


def _option_value_is_valid(option: dict[str, Any], value: Any) -> bool:
    option_type = option["type"]
    if option_type == "boolean":
        return type(value) is bool
    if option_type == "integer":
        if type(value) is not int:
            return False
        minimum = option.get("minimum")
        maximum = option.get("maximum")
        return (minimum is None or value >= minimum) and (maximum is None or value <= maximum)
    if not isinstance(value, str):
        return False
    if option_type == "enum":
        return value in option["values"]
    minimum_length = option.get("minimumLength")
    maximum_length = option.get("maximumLength")
    return (minimum_length is None or len(value) >= minimum_length) and (
        maximum_length is None or len(value) <= maximum_length
    )


def validate_locked_result_data(
    lock: Any,
    project: Any,
    author_manifests: Iterable[Any],
    validators: ContractValidators,
    lock_path: str = PACKAGE_LOCK_NAME,
    project_path: str = PROJECT_MANIFEST_NAME,
) -> list[Diagnostic]:
    """Validate a selected result against Project and pinned author manifests without solving."""

    diagnostics = validate_manifest_data(lock, lock_path, validators)
    diagnostics.extend(validate_manifest_data(project, project_path, validators))
    if diagnostics:
        return sorted(diagnostics, key=_diagnostic_sort_key)

    manifests_by_id: dict[str, dict[str, Any]] = {}
    for manifest_index, manifest in enumerate(author_manifests):
        manifest_path = f"candidate-{manifest_index}/{PACKAGE_MANIFEST_NAME}"
        manifest_diagnostics = validate_manifest_data(manifest, manifest_path, validators)
        diagnostics.extend(manifest_diagnostics)
        if manifest_diagnostics:
            continue
        identity = manifest["id"]
        if identity in manifests_by_id:
            diagnostics.append(
                Diagnostic(
                    code="lock.candidate.duplicate-manifest",
                    manifest_path=lock_path,
                    pointer="/nodes",
                    message=f"multiple pinned author manifests were provided for '{identity}'",
                )
            )
        else:
            manifests_by_id[identity] = manifest
    if diagnostics:
        return sorted(diagnostics, key=_diagnostic_sort_key)

    expected_project_integrity = compute_project_manifest_integrity(project)
    if lock["inputs"]["projectManifestIntegrity"] != expected_project_integrity:
        diagnostics.append(
            Diagnostic(
                code="lock.input.project-manifest-stale",
                manifest_path=lock_path,
                pointer="/inputs/projectManifestIntegrity",
                message="lock does not match the normalized Project Manifest",
            )
        )

    nodes_by_id = {node["id"]: node for node in lock["nodes"]}
    node_indices = {node["id"]: index for index, node in enumerate(lock["nodes"])}

    for collection, project_collection in (
        ("directPackages", "directPackages"),
        ("directFeatureSets", "directFeatureSets"),
    ):
        requirements = {item["id"]: item for item in project[project_collection]}
        roots = {item["id"]: item for item in lock["roots"][collection]}
        for identity in sorted(requirements.keys() - roots.keys()):
            diagnostics.append(
                Diagnostic(
                    code="lock.root.missing-project-selection",
                    manifest_path=lock_path,
                    pointer=f"/roots/{collection}",
                    message=f"direct project selection '{identity}' is absent from lock roots",
                )
            )
        for root_index, root in enumerate(lock["roots"][collection]):
            identity = root["id"]
            requirement = requirements.get(identity)
            if requirement is None:
                diagnostics.append(
                    Diagnostic(
                        code="lock.root.undeclared-project-selection",
                        manifest_path=lock_path,
                        pointer=f"/roots/{collection}/{root_index}/id",
                        message=f"lock root '{identity}' is not directly selected by the project",
                    )
                )
            elif not _version_satisfies_constraint(root["version"], requirement["version"]):
                diagnostics.append(
                    Diagnostic(
                        code="lock.root.constraint-mismatch",
                        manifest_path=lock_path,
                        pointer=f"/roots/{collection}/{root_index}/version",
                        message=(
                            f"locked version '{root['version']}' does not satisfy the direct "
                            f"constraint for '{identity}'"
                        ),
                    )
                )

    engine_api_version = lock["inputs"]["engineApiVersion"]
    for node_index, node in enumerate(lock["nodes"]):
        identity = node["id"]
        manifest = manifests_by_id.get(identity)
        if manifest is None:
            diagnostics.append(
                Diagnostic(
                    code="lock.candidate.missing-manifest",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/id",
                    message=f"no pinned author manifest was provided for '{identity}'",
                )
            )
            continue
        if manifest["version"] != node["version"]:
            diagnostics.append(
                Diagnostic(
                    code="lock.candidate.version-mismatch",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/version",
                    message=(
                        f"locked version '{node['version']}' does not match pinned manifest "
                        f"version '{manifest['version']}'"
                    ),
                )
            )
        if manifest["packageKind"] != node["packageKind"]:
            diagnostics.append(
                Diagnostic(
                    code="lock.candidate.kind-mismatch",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/packageKind",
                    message=(
                        f"locked kind '{node['packageKind']}' does not match pinned manifest "
                        f"kind '{manifest['packageKind']}'"
                    ),
                )
            )
        if not _version_satisfies_constraint(engine_api_version, manifest["engineApi"]):
            diagnostics.append(
                Diagnostic(
                    code="lock.candidate.engine-api-mismatch",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/version",
                    message=(
                        f"engine API '{engine_api_version}' does not satisfy '{identity}' "
                        "engineApi constraint"
                    ),
                )
            )

        expected_dependencies: dict[str, tuple[dict[str, Any], str]] = {}
        if manifest["packageKind"] == "installable-capability":
            expected_dependencies.update(
                (requirement["id"], (requirement, "installable-capability"))
                for requirement in manifest["dependencies"]
            )
        else:
            expected_dependencies.update(
                (requirement["id"], (requirement, "installable-capability"))
                for requirement in manifest["packages"]
            )
            expected_dependencies.update(
                (requirement["id"], (requirement, "feature-set"))
                for requirement in manifest["featureSets"]
            )
        locked_dependencies = {item["id"]: item for item in node["dependencies"]}
        for dependency in sorted(expected_dependencies.keys() - locked_dependencies.keys()):
            diagnostics.append(
                Diagnostic(
                    code="lock.edge.missing-author-dependency",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/dependencies",
                    message=f"author dependency '{dependency}' is absent from exact edges",
                )
            )
        for dependency_index, reference in enumerate(node["dependencies"]):
            dependency = reference["id"]
            expected = expected_dependencies.get(dependency)
            pointer = f"/nodes/{node_index}/dependencies/{dependency_index}"
            if expected is None:
                diagnostics.append(
                    Diagnostic(
                        code="lock.edge.undeclared-author-dependency",
                        manifest_path=lock_path,
                        pointer=f"{pointer}/id",
                        message=f"exact edge '{dependency}' is not declared by the author manifest",
                    )
                )
                continue
            requirement, required_kind = expected
            if reference["packageKind"] != required_kind:
                diagnostics.append(
                    Diagnostic(
                        code="lock.edge.author-kind-mismatch",
                        manifest_path=lock_path,
                        pointer=f"{pointer}/packageKind",
                        message=f"author dependency '{dependency}' requires kind '{required_kind}'",
                    )
                )
            selected = nodes_by_id.get(dependency)
            if selected is not None and not _version_satisfies_constraint(
                selected["version"], requirement["version"]
            ):
                diagnostics.append(
                    Diagnostic(
                        code="lock.edge.constraint-mismatch",
                        manifest_path=lock_path,
                        pointer=f"{pointer}/version",
                        message=(
                            f"selected version '{selected['version']}' does not satisfy "
                            f"'{identity}' constraint for '{dependency}'"
                        ),
                    )
                )

    for option_group_index, option_group in enumerate(project["packageOptions"]):
        package_id = option_group["packageId"]
        node = nodes_by_id.get(package_id)
        if node is None:
            diagnostics.append(
                Diagnostic(
                    code="lock.option.orphan-package",
                    manifest_path=project_path,
                    pointer=f"/packageOptions/{option_group_index}/packageId",
                    message=f"option target '{package_id}' is not present in the locked graph",
                )
            )
            continue
        if node["packageKind"] != "installable-capability":
            diagnostics.append(
                Diagnostic(
                    code="lock.option.invalid-package-kind",
                    manifest_path=project_path,
                    pointer=f"/packageOptions/{option_group_index}/packageId",
                    message=f"option target '{package_id}' is not an installable package",
                )
            )
            continue
        manifest = manifests_by_id.get(package_id)
        if manifest is None or manifest["packageKind"] != "installable-capability":
            continue
        options_by_id = {option["id"]: option for option in manifest["options"]}
        for option_index, option_value in enumerate(option_group["values"]):
            option = options_by_id.get(option_value["id"])
            pointer = f"/packageOptions/{option_group_index}/values/{option_index}"
            if option is None:
                diagnostics.append(
                    Diagnostic(
                        code="lock.option.unknown-id",
                        manifest_path=project_path,
                        pointer=f"{pointer}/id",
                        message=(
                            f"package '{package_id}' does not declare option "
                            f"'{option_value['id']}'"
                        ),
                    )
                )
            elif not _option_value_is_valid(option, option_value["value"]):
                diagnostics.append(
                    Diagnostic(
                        code="lock.option.invalid-value",
                        manifest_path=project_path,
                        pointer=f"{pointer}/value",
                        message=(
                            f"value for '{package_id}:{option_value['id']}' does not satisfy "
                            f"declared option type '{option['type']}'"
                        ),
                    )
                )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _contract_kind(manifest: Any, manifest_path: str) -> str | None:
    if Path(manifest_path).name == HOST_PROFILE_NAME:
        return "host-profile"
    if Path(manifest_path).name == PACKAGE_LOCK_NAME:
        return "lock"
    if Path(manifest_path).name == PROJECT_MANIFEST_NAME:
        return "project"
    if not isinstance(manifest, dict):
        return None
    if manifest.get("schema") == "com.asharia.project-packages":
        return "project"
    if manifest.get("schema") == "com.asharia.package-lock":
        return "lock"
    if manifest.get("schema") == "com.asharia.host-profile":
        return "host-profile"
    if manifest.get("schemaVersion") == 1 and manifest.get("packageKind") == "source-boundary":
        return "source-boundary"
    package_kind = manifest.get("packageKind")
    if package_kind == "installable-capability":
        return "installable"
    if package_kind == "feature-set":
        return "feature-set"
    return None


def validate_manifest_data(
    manifest: Any,
    manifest_path: str,
    validators: ContractValidators | Draft202012Validator,
) -> list[Diagnostic]:
    """Validate already-parsed JSON and return stable, sorted diagnostics."""

    if isinstance(validators, Draft202012Validator):
        schema_diagnostics = _schema_diagnostics(manifest, manifest_path, validators)
        if schema_diagnostics:
            return schema_diagnostics
        return _installable_semantic_diagnostics(manifest, manifest_path)

    kind = _contract_kind(manifest, manifest_path)
    if kind == "source-boundary":
        return []
    if kind is None:
        return [
            Diagnostic(
                code="contract.manifest.unsupported-kind",
                manifest_path=manifest_path,
                pointer="",
                message="manifest does not declare a supported package-runtime author contract",
            )
        ]
    if kind == "host-profile":
        validator = validators.host_profile
        schema_code = "host-profile.manifest.schema"
        semantic_validator = _host_profile_semantic_diagnostics
    elif kind == "lock":
        validator = validators.lock
        schema_code = "lock.manifest.schema"
        semantic_validator = _lock_semantic_diagnostics
    elif kind == "project":
        validator = validators.project
        schema_code = "project.manifest.schema"
        semantic_validator = _project_semantic_diagnostics
    elif kind == "feature-set":
        validator = validators.feature_set
        schema_code = "feature-set.manifest.schema"
        semantic_validator = _feature_set_semantic_diagnostics
    else:
        validator = validators.installable
        schema_code = "package.manifest.schema"
        semantic_validator = _installable_semantic_diagnostics

    schema_diagnostics = _schema_diagnostics(manifest, manifest_path, validator, schema_code)
    if schema_diagnostics:
        return schema_diagnostics
    return semantic_validator(manifest, manifest_path)


def validate_manifest_file(
    path: Path, validators: ContractValidators | Draft202012Validator
) -> list[Diagnostic]:
    """Read and validate one package-runtime author contract."""

    manifest_path = path.as_posix()
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return [
            Diagnostic(
                code="contract.manifest.json",
                manifest_path=manifest_path,
                pointer="",
                message=f"cannot read JSON: {exc}",
            )
        ]
    return validate_manifest_data(manifest, manifest_path, validators)


def discover_contract_manifests(root: Path) -> list[Path]:
    """Discover package-runtime contracts while routing v1 source boundaries elsewhere."""

    paths: list[Path] = []
    candidates = (
        list(root.rglob(PACKAGE_MANIFEST_NAME))
        + list(root.rglob(PROJECT_MANIFEST_NAME))
        + list(root.rglob(PACKAGE_LOCK_NAME))
        + list(root.rglob(HOST_PROFILE_NAME))
    )
    for path in candidates:
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


def discover_installable_manifests(root: Path) -> list[Path]:
    """Compatibility alias for callers predating Project Manifest support."""

    return discover_contract_manifests(root)


def validate_feature_set_graph(
    manifests: Iterable[tuple[str, dict[str, Any]]],
) -> list[Diagnostic]:
    """Reject cycles in a caller-provided graph containing one selected version per identity."""

    grouped: dict[str, list[tuple[str, dict[str, Any]]]] = {}
    for manifest_path, manifest in manifests:
        grouped.setdefault(manifest["id"], []).append((manifest_path, manifest))
    selected = {
        identity: candidates[0]
        for identity, candidates in grouped.items()
        if len(candidates) == 1
    }

    state: dict[str, int] = {}
    stack: list[str] = []
    diagnostics: list[Diagnostic] = []
    reported_edges: set[tuple[str, str]] = set()

    def visit(identity: str) -> None:
        state[identity] = 1
        stack.append(identity)
        manifest_path, manifest = selected[identity]
        for member_index, member in enumerate(manifest["featureSets"]):
            dependency = member["id"]
            if dependency == identity or dependency not in selected:
                continue
            if state.get(dependency, 0) == 0:
                visit(dependency)
            elif state.get(dependency) == 1 and (identity, dependency) not in reported_edges:
                cycle_start = stack.index(dependency)
                cycle = stack[cycle_start:] + [dependency]
                diagnostics.append(
                    Diagnostic(
                        code="feature-set.member.cycle",
                        manifest_path=manifest_path,
                        pointer=f"/featureSets/{member_index}/id",
                        message=f"Feature Set cycle: {' -> '.join(cycle)}",
                    )
                )
                reported_edges.add((identity, dependency))
        stack.pop()
        state[identity] = 2

    for identity in sorted(selected):
        if state.get(identity, 0) == 0:
            visit(identity)
    return sorted(diagnostics, key=_diagnostic_sort_key)


def validate_manifest_files(paths: Iterable[Path], validators: ContractValidators) -> list[Diagnostic]:
    """Validate contracts and graph-local Feature Set cycles without resolving candidates."""

    diagnostics: list[Diagnostic] = []
    valid_feature_sets: list[tuple[str, dict[str, Any]]] = []
    for path in paths:
        manifest_path = path.as_posix()
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            diagnostics.append(
                Diagnostic(
                    code="contract.manifest.json",
                    manifest_path=manifest_path,
                    pointer="",
                    message=f"cannot read JSON: {exc}",
                )
            )
            continue
        file_diagnostics = validate_manifest_data(manifest, manifest_path, validators)
        diagnostics.extend(file_diagnostics)
        if not file_diagnostics and _contract_kind(manifest, manifest_path) == "feature-set":
            valid_feature_sets.append((manifest_path, manifest))
    diagnostics.extend(validate_feature_set_graph(valid_feature_sets))
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _normalize_version_constraint(constraint: dict[str, Any]) -> dict[str, Any]:
    if constraint["kind"] == "exact":
        return {"kind": "exact", "version": constraint["version"]}
    return {
        "kind": "range",
        "minimumInclusive": constraint["minimumInclusive"],
        "maximumExclusive": constraint["maximumExclusive"],
        "allowPrerelease": constraint["allowPrerelease"],
    }


def normalize_project_manifest(manifest: dict[str, Any]) -> dict[str, Any]:
    """Return the deterministic semantic representation used for writing and hashing."""

    def normalize_requirements(requirements: list[dict[str, Any]]) -> list[dict[str, Any]]:
        return [
            {"id": item["id"], "version": _normalize_version_constraint(item["version"])}
            for item in sorted(requirements, key=lambda value: value["id"])
        ]

    return {
        "schema": "com.asharia.project-packages",
        "schemaVersion": 1,
        "directPackages": normalize_requirements(manifest["directPackages"]),
        "directFeatureSets": normalize_requirements(manifest["directFeatureSets"]),
        "packageOptions": [
            {
                "packageId": package_options["packageId"],
                "values": [
                    {"id": option["id"], "value": option["value"]}
                    for option in sorted(package_options["values"], key=lambda value: value["id"])
                ],
            }
            for package_options in sorted(
                manifest["packageOptions"], key=lambda value: value["packageId"]
            )
        ],
    }


def render_normalized_project_manifest(manifest: dict[str, Any]) -> str:
    """Render normalized Project Manifest JSON as UTF-8-compatible LF text."""

    return json.dumps(normalize_project_manifest(manifest), ensure_ascii=False, indent=2) + "\n"


def write_normalized_project_manifest(path: Path, manifest: dict[str, Any]) -> None:
    """Write one normalized Project Manifest using UTF-8 without BOM and LF endings."""

    path.write_text(render_normalized_project_manifest(manifest), encoding="utf-8", newline="\n")


def _normalize_integrity(integrity: dict[str, Any]) -> dict[str, Any]:
    return {"algorithm": integrity["algorithm"], "digest": integrity["digest"]}


def _normalize_exact_reference(reference: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": reference["id"],
        "version": reference["version"],
        "packageKind": reference["packageKind"],
    }


def _normalize_lock_source(source: dict[str, Any]) -> dict[str, Any]:
    if source["kind"] == "bundled":
        return {
            "kind": "bundled",
            "distributionId": source["distributionId"],
            "relativePath": source["relativePath"],
        }
    if source["kind"] == "project-embedded":
        return {"kind": "project-embedded", "relativePath": source["relativePath"]}
    return {"kind": "local", "sourceId": source["sourceId"]}


def normalize_lock_manifest(manifest: dict[str, Any]) -> dict[str, Any]:
    """Return the deterministic representation of an already-validated lockfile."""

    def normalize_references(references: list[dict[str, Any]]) -> list[dict[str, Any]]:
        return [
            _normalize_exact_reference(reference)
            for reference in sorted(references, key=lambda value: value["id"])
        ]

    return {
        "schema": "com.asharia.package-lock",
        "schemaVersion": 1,
        "resolver": {
            "version": manifest["resolver"]["version"],
            "policyVersion": manifest["resolver"]["policyVersion"],
        },
        "inputs": {
            "engineApiVersion": manifest["inputs"]["engineApiVersion"],
            "projectManifestIntegrity": _normalize_integrity(
                manifest["inputs"]["projectManifestIntegrity"]
            ),
        },
        "roots": {
            "directPackages": normalize_references(manifest["roots"]["directPackages"]),
            "directFeatureSets": normalize_references(
                manifest["roots"]["directFeatureSets"]
            ),
        },
        "nodes": [
            {
                "id": node["id"],
                "version": node["version"],
                "packageKind": node["packageKind"],
                "source": _normalize_lock_source(node["source"]),
                "manifestIntegrity": _normalize_integrity(node["manifestIntegrity"]),
                "payloadIntegrity": _normalize_integrity(node["payloadIntegrity"]),
                "dependencies": normalize_references(node["dependencies"]),
            }
            for node in sorted(manifest["nodes"], key=lambda value: value["id"])
        ],
    }


def render_normalized_lock_manifest(manifest: dict[str, Any]) -> str:
    """Render normalized lockfile JSON as UTF-8-compatible LF text."""

    return json.dumps(normalize_lock_manifest(manifest), ensure_ascii=False, indent=2) + "\n"


def write_normalized_lock_manifest(path: Path, manifest: dict[str, Any]) -> None:
    """Write one normalized lockfile using UTF-8 without BOM and LF endings."""

    path.write_text(render_normalized_lock_manifest(manifest), encoding="utf-8", newline="\n")


def normalize_host_profile(manifest: dict[str, Any]) -> dict[str, Any]:
    """Return the deterministic representation of an already-validated Host Profile."""

    role_order = {role: index for index, role in enumerate(MODULE_ROLE_ORDER)}
    shipping_order = {
        shipping_class: index
        for index, shipping_class in enumerate(SHIPPING_CLASS_ORDER)
    }
    return {
        "schema": "com.asharia.host-profile",
        "schemaVersion": 1,
        "hostKind": manifest["hostKind"],
        "targetPlatform": manifest["targetPlatform"],
        "requiredRoles": sorted(manifest["requiredRoles"], key=role_order.__getitem__),
        "allowedRoles": sorted(manifest["allowedRoles"], key=role_order.__getitem__),
        "allowedShippingClasses": sorted(
            manifest["allowedShippingClasses"], key=shipping_order.__getitem__
        ),
        "contributionFilter": {"mode": manifest["contributionFilter"]["mode"]},
        "grantedCapabilities": sorted(manifest["grantedCapabilities"]),
    }


def render_normalized_host_profile(manifest: dict[str, Any]) -> str:
    """Render normalized Host Profile JSON as UTF-8-compatible LF text."""

    return json.dumps(normalize_host_profile(manifest), ensure_ascii=False, indent=2) + "\n"


def write_normalized_host_profile(path: Path, manifest: dict[str, Any]) -> None:
    """Write one normalized Host Profile using UTF-8 without BOM and LF endings."""

    path.write_text(render_normalized_host_profile(manifest), encoding="utf-8", newline="\n")


def compute_bytes_integrity(data: bytes) -> dict[str, str]:
    """Compute the v1 structured SHA-256 integrity record for exact bytes."""

    return {"algorithm": "sha256", "digest": hashlib.sha256(data).hexdigest()}


def compute_project_manifest_integrity(manifest: dict[str, Any]) -> dict[str, str]:
    """Hash normalized Project Manifest UTF-8 bytes."""

    return compute_bytes_integrity(render_normalized_project_manifest(manifest).encode("utf-8"))


def compute_manifest_file_integrity(path: Path) -> dict[str, str]:
    """Hash exact author-manifest file bytes."""

    return compute_bytes_integrity(path.read_bytes())


def compute_package_tree_integrity(root: Path) -> dict[str, str]:
    """Hash one package payload using the asharia-package-tree-v1 byte domain."""

    root_is_junction = getattr(root, "is_junction", lambda: False)()
    if root.is_symlink() or root_is_junction:
        raise PackageTreeIntegrityError(
            "link",
            "",
            "package payload root cannot be a link or junction",
        )
    manifest_path = root / PACKAGE_MANIFEST_NAME
    if not manifest_path.is_file() or manifest_path.is_symlink():
        raise PackageTreeIntegrityError(
            "manifest-missing",
            PACKAGE_MANIFEST_NAME,
            f"package payload root must contain regular file '{PACKAGE_MANIFEST_NAME}'",
        )

    entries: list[tuple[bytes, Path]] = []
    case_folded_paths: dict[str, str] = {}

    def visit(directory: Path, relative_parts: tuple[str, ...]) -> None:
        for child in sorted(
            directory.iterdir(),
            key=lambda value: value.name.encode("utf-8", errors="surrogatepass"),
        ):
            if not relative_parts and child.name in PACKAGE_TREE_ROOT_EXCLUDES:
                continue
            child_parts = relative_parts + (child.name,)
            relative_path = "/".join(child_parts)
            is_junction = getattr(child, "is_junction", lambda: False)()
            if child.is_symlink() or is_junction:
                raise PackageTreeIntegrityError(
                    "link",
                    relative_path,
                    f"package payload cannot contain link '{relative_path}'",
                )
            normalized_name = unicodedata.normalize("NFC", child.name)
            if normalized_name != child.name:
                raise PackageTreeIntegrityError(
                    "path-not-nfc",
                    relative_path,
                    f"package payload path is not Unicode NFC: '{relative_path}'",
                )
            if not _is_normalized_relative_path(relative_path):
                raise PackageTreeIntegrityError(
                    "path-not-portable",
                    relative_path,
                    f"package payload path is not portable: '{relative_path}'",
                )
            folded = relative_path.casefold()
            previous = case_folded_paths.get(folded)
            if previous is not None and previous != relative_path:
                raise PackageTreeIntegrityError(
                    "case-collision",
                    relative_path,
                    f"package payload paths collide by case: '{previous}' and '{relative_path}'",
                )
            case_folded_paths[folded] = relative_path
            if child.is_dir():
                visit(child, child_parts)
            elif child.is_file():
                entries.append((relative_path.encode("utf-8"), child))
            else:
                raise PackageTreeIntegrityError(
                    "non-regular",
                    relative_path,
                    f"package payload contains non-regular entry '{relative_path}'",
                )

    visit(root, ())
    digest = hashlib.sha256()
    digest.update(PACKAGE_TREE_HEADER)
    for relative_path_bytes, path in sorted(entries, key=lambda item: item[0]):
        content = path.read_bytes()
        digest.update(len(relative_path_bytes).to_bytes(8, "big"))
        digest.update(relative_path_bytes)
        digest.update(len(content).to_bytes(8, "big"))
        digest.update(content)
    return {"algorithm": "sha256", "digest": digest.hexdigest()}


def validate_locked_candidate_integrity(
    lock: dict[str, Any],
    candidate_roots: dict[str, Path],
    lock_path: str = PACKAGE_LOCK_NAME,
) -> list[Diagnostic]:
    """Verify selected candidate payloads for an already schema-valid lockfile."""

    diagnostics: list[Diagnostic] = []
    for node_index, node in enumerate(lock["nodes"]):
        identity = node["id"]
        root = candidate_roots.get(identity)
        if root is None:
            diagnostics.append(
                Diagnostic(
                    code="lock.source.unavailable",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/source",
                    message=f"selected source for '{identity}' is unavailable",
                )
            )
            continue
        try:
            manifest_integrity = compute_manifest_file_integrity(root / PACKAGE_MANIFEST_NAME)
            payload_integrity = compute_package_tree_integrity(root)
        except (OSError, ValueError) as exc:
            diagnostics.append(
                Diagnostic(
                    code="lock.integrity.payload-unreadable",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/payloadIntegrity",
                    message=f"cannot verify payload for '{identity}': {exc}",
                )
            )
            continue
        if manifest_integrity != node["manifestIntegrity"]:
            diagnostics.append(
                Diagnostic(
                    code="lock.integrity.manifest-mismatch",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/manifestIntegrity",
                    message=f"author manifest bytes changed for '{identity}'",
                )
            )
        if payload_integrity != node["payloadIntegrity"]:
            diagnostics.append(
                Diagnostic(
                    code="lock.integrity.payload-mismatch",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/payloadIntegrity",
                    message=f"package payload tree changed for '{identity}'",
                )
            )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _module_supports_host_target(module: dict[str, Any], profile: dict[str, Any]) -> bool:
    platforms = module["platforms"]
    return profile["hostKind"] in module["hostKinds"] and (
        ANY_PLATFORM_ID in platforms or profile["targetPlatform"] in platforms
    )


def select_host_profile_data(
    lock: Any,
    author_manifests: Iterable[Any],
    profile: Any,
    validators: ContractValidators,
    lock_path: str = PACKAGE_LOCK_NAME,
    profile_path: str = HOST_PROFILE_NAME,
) -> tuple[HostProjection | None, list[Diagnostic]]:
    """Project a verified lock into logical Host selections without creating a plan."""

    diagnostics = validate_manifest_data(lock, lock_path, validators)
    diagnostics.extend(validate_manifest_data(profile, profile_path, validators))
    if diagnostics:
        return None, sorted(diagnostics, key=_diagnostic_sort_key)

    manifests_by_id: dict[str, dict[str, Any]] = {}
    manifest_paths_by_id: dict[str, str] = {}
    for manifest_index, manifest in enumerate(author_manifests):
        manifest_path = f"candidate-{manifest_index}/{PACKAGE_MANIFEST_NAME}"
        manifest_diagnostics = validate_manifest_data(manifest, manifest_path, validators)
        diagnostics.extend(manifest_diagnostics)
        if manifest_diagnostics:
            continue
        kind = _contract_kind(manifest, manifest_path)
        if kind not in {"installable", "feature-set"}:
            diagnostics.append(
                Diagnostic(
                    code="host.package.invalid-manifest-kind",
                    manifest_path=manifest_path,
                    pointer="",
                    message="Host projection accepts only pinned installable or Feature Set manifests",
                )
            )
            continue
        identity = manifest["id"]
        if identity in manifests_by_id:
            diagnostics.append(
                Diagnostic(
                    code="host.package.duplicate-manifest",
                    manifest_path=manifest_path,
                    pointer="/id",
                    message=f"multiple pinned author manifests were provided for '{identity}'",
                )
            )
            continue
        manifests_by_id[identity] = manifest
        manifest_paths_by_id[identity] = manifest_path
    if diagnostics:
        return None, sorted(diagnostics, key=_diagnostic_sort_key)

    installable_nodes: list[tuple[int, dict[str, Any], dict[str, Any]]] = []
    for node_index, node in enumerate(lock["nodes"]):
        if node["packageKind"] != "installable-capability":
            continue
        identity = node["id"]
        manifest = manifests_by_id.get(identity)
        if manifest is None:
            diagnostics.append(
                Diagnostic(
                    code="host.package.missing-manifest",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/id",
                    message=f"no pinned installable manifest was provided for '{identity}'",
                )
            )
            continue
        if manifest["packageKind"] != "installable-capability":
            diagnostics.append(
                Diagnostic(
                    code="host.package.kind-mismatch",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/packageKind",
                    message=(
                        f"locked installable '{identity}' is backed by manifest kind "
                        f"'{manifest['packageKind']}'"
                    ),
                )
            )
        if manifest["version"] != node["version"]:
            diagnostics.append(
                Diagnostic(
                    code="host.package.version-mismatch",
                    manifest_path=lock_path,
                    pointer=f"/nodes/{node_index}/version",
                    message=(
                        f"locked version '{node['version']}' does not match pinned manifest "
                        f"version '{manifest['version']}' for '{identity}'"
                    ),
                )
            )
        installable_nodes.append((node_index, node, manifest))
    if diagnostics:
        return None, sorted(diagnostics, key=_diagnostic_sort_key)

    required_roles = set(profile["requiredRoles"])
    allowed_roles = set(profile["allowedRoles"])
    allowed_shipping = set(profile["allowedShippingClasses"])
    granted_capabilities = set(profile["grantedCapabilities"])
    module_selections: list[HostModuleSelection] = []
    contribution_selections: list[HostContributionSelection] = []

    for _, node, manifest in sorted(installable_nodes, key=lambda item: item[1]["id"]):
        package_id = node["id"]
        package_version = node["version"]
        manifest_path = manifest_paths_by_id[package_id]
        modules = manifest["modules"]
        modules_by_id = {module["id"]: module for module in modules}
        module_indices = {module["id"]: index for index, module in enumerate(modules)}
        roots = {
            module["id"]
            for module in modules
            if module["role"] in required_roles
            and _module_supports_host_target(module, profile)
        }
        compatible_contributions: list[dict[str, Any]] = []
        if profile["contributionFilter"]["mode"] == "allow-compatible":
            for contribution in manifest["contributions"]:
                owner = modules_by_id[contribution["module"]]
                if (
                    profile["hostKind"] in contribution["hostKinds"]
                    and _module_supports_host_target(owner, profile)
                    and owner["role"] in allowed_roles
                    and owner["shippingClass"] in allowed_shipping
                    and contribution["shippingClass"] in allowed_shipping
                ):
                    compatible_contributions.append(contribution)
                    roots.add(owner["id"])

        processed: set[str] = set()
        selected: set[str] = set()

        def visit(
            module_id: str,
            parent_module_id: str | None = None,
            dependency_index: int | None = None,
        ) -> None:
            if module_id in processed:
                return
            processed.add(module_id)
            module = modules_by_id[module_id]
            module_index = module_indices[module_id]
            edge_pointer = (
                f"/modules/{module_indices[parent_module_id]}/dependsOn/{dependency_index}"
                if parent_module_id is not None and dependency_index is not None
                else None
            )
            valid = True
            if profile["hostKind"] not in module["hostKinds"]:
                diagnostics.append(
                    Diagnostic(
                        code="host.module.host-closure",
                        manifest_path=manifest_path,
                        pointer=edge_pointer or f"/modules/{module_index}/hostKinds",
                        message=(
                            f"selected module '{module_id}' is unavailable for host "
                            f"'{profile['hostKind']}'"
                        ),
                    )
                )
                valid = False
            platforms = module["platforms"]
            if ANY_PLATFORM_ID not in platforms and profile["targetPlatform"] not in platforms:
                diagnostics.append(
                    Diagnostic(
                        code="host.module.platform-closure",
                        manifest_path=manifest_path,
                        pointer=edge_pointer or f"/modules/{module_index}/platforms",
                        message=(
                            f"selected module '{module_id}' is unavailable for platform "
                            f"'{profile['targetPlatform']}'"
                        ),
                    )
                )
                valid = False
            if module["role"] not in allowed_roles:
                diagnostics.append(
                    Diagnostic(
                        code="host.module.role-closure",
                        manifest_path=manifest_path,
                        pointer=edge_pointer or f"/modules/{module_index}/role",
                        message=(
                            f"selected module '{module_id}' role '{module['role']}' is not allowed "
                            f"by host '{profile['hostKind']}'"
                        ),
                    )
                )
                valid = False
            if module["shippingClass"] not in allowed_shipping:
                diagnostics.append(
                    Diagnostic(
                        code="host.module.shipping-closure",
                        manifest_path=manifest_path,
                        pointer=edge_pointer or f"/modules/{module_index}/shippingClass",
                        message=(
                            f"selected module '{module_id}' shipping class "
                            f"'{module['shippingClass']}' is not allowed by host "
                            f"'{profile['hostKind']}'"
                        ),
                    )
                )
                valid = False
            for capability_index, capability in sorted(
                enumerate(module["requiredCapabilities"]), key=lambda item: item[1]
            ):
                if capability not in granted_capabilities:
                    diagnostics.append(
                        Diagnostic(
                            code="host.module.capability-denied",
                            manifest_path=manifest_path,
                            pointer=(
                                f"/modules/{module_index}/requiredCapabilities/"
                                f"{capability_index}"
                            ),
                            message=(
                                f"selected module '{module_id}' requires capability "
                                f"'{capability}' that the Host Profile does not grant"
                            ),
                        )
                    )
                    valid = False
            if not valid:
                return

            selected.add(module_id)
            for child_index, dependency in sorted(
                enumerate(module["dependsOn"]), key=lambda item: item[1]
            ):
                visit(dependency, module_id, child_index)

        for root in sorted(roots):
            visit(root)

        module_selections.extend(
            HostModuleSelection(package_id, package_version, module_id)
            for module_id in sorted(selected)
        )
        contribution_selections.extend(
            HostContributionSelection(
                package_id,
                package_version,
                contribution["id"],
                contribution["kind"],
                contribution["module"],
            )
            for contribution in compatible_contributions
            if contribution["module"] in selected
        )

    if diagnostics:
        return None, sorted(diagnostics, key=_diagnostic_sort_key)
    return (
        HostProjection(
            host_kind=profile["hostKind"],
            target_platform=profile["targetPlatform"],
            modules=tuple(sorted(module_selections)),
            contributions=tuple(sorted(contribution_selections)),
        ),
        [],
    )


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifests", nargs="*", type=Path, help="explicit contracts to validate")
    parser.add_argument("--root", type=Path, default=REPOSITORY_ROOT, help="repository root")
    parser.add_argument(
        "--schema-root",
        type=Path,
        default=DEFAULT_SCHEMA_ROOT,
        help="directory containing package-runtime JSON Schemas",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    root = args.root.resolve()
    schema_root = args.schema_root if args.schema_root.is_absolute() else root / args.schema_root
    try:
        validators = load_contract_validators(schema_root)
    except (OSError, UnicodeError, json.JSONDecodeError, SchemaError) as exc:
        print(f"Package contract schemas are invalid: {exc}", file=sys.stderr)
        return 2

    paths = [path if path.is_absolute() else root / path for path in args.manifests]
    if not paths:
        paths = discover_contract_manifests(root)

    diagnostics = validate_manifest_files(paths, validators)
    if diagnostics:
        print(f"Package contract validation failed with {len(diagnostics)} error(s):", file=sys.stderr)
        for diagnostic in diagnostics:
            print(f"  - {diagnostic.render()}", file=sys.stderr)
        return 1

    print(f"Package contracts OK: contracts={len(paths)} schema=draft-2020-12")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
