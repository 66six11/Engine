"""Deterministic per-host logical composition planning for verified package graphs."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Callable, Iterable, TypeAlias

from tools import check_package_contracts as contracts
from tools.package_candidates import PackageCandidate
from tools.package_lock_verification import LockedGraphVerificationResult


HOST_COMPOSITION_PLAN_NAME = "asharia.host-composition-plan.json"
HOST_COMPOSITION_PLAN_SCHEMA = "com.asharia.host-composition-plan"
HOST_COMPOSITION_PLAN_SCHEMA_VERSION = 1
ENTRY_DIMENSION_ORDER = ("runtime", "authoring", "cook", "diagnostics")
OPTION_AFFECT_ORDER = ("build", "activation", "cook")

OptionValue: TypeAlias = bool | int | str


@dataclass(frozen=True, order=True)
class IntegrityRecord:
    """Immutable structured integrity evidence."""

    algorithm: str
    digest: str


@dataclass(frozen=True, order=True)
class ExactPackageReference:
    """One exact package graph reference."""

    package_id: str
    package_version: str
    package_kind: str


@dataclass(frozen=True)
class ResolvedPackageComposition:
    """One exact resolved node in dependency-first composition order."""

    package: ExactPackageReference
    dependencies: tuple[ExactPackageReference, ...]


@dataclass(frozen=True)
class EffectivePackageOption:
    """One author-declared option after applying the Project override."""

    option_id: str
    option_type: str
    affects: tuple[str, ...]
    value: OptionValue


@dataclass(frozen=True)
class HostModuleComposition:
    """One Host-selected logical module and its package-local dependencies."""

    module_id: str
    role: str
    shipping_class: str
    required_capabilities: tuple[str, ...]
    dependencies: tuple[str, ...]


@dataclass(frozen=True)
class HostPackageComposition:
    """One locked installable package projected for a Host Profile."""

    package: ExactPackageReference
    dependencies: tuple[ExactPackageReference, ...]
    options: tuple[EffectivePackageOption, ...]
    modules: tuple[HostModuleComposition, ...]


@dataclass(frozen=True, order=True)
class HostEntryComposition:
    """One selected named logical entry without lifecycle semantics."""

    dimension: str
    package_id: str
    package_version: str
    module_id: str


@dataclass(frozen=True, order=True)
class HostContributionComposition:
    """One selected contribution bound to its selected owner module."""

    package_id: str
    package_version: str
    contribution_id: str
    contribution_kind: str
    owner_module_id: str


@dataclass(frozen=True)
class HostCompositionPlan:
    """Canonical backend-neutral logical composition for one Host invocation."""

    engine_api_version: str
    project_manifest_integrity: IntegrityRecord
    locked_graph_integrity: IntegrityRecord
    host_profile_integrity: IntegrityRecord
    host_kind: str
    target_platform: str
    granted_capabilities: tuple[str, ...]
    direct_packages: tuple[ExactPackageReference, ...]
    direct_feature_sets: tuple[ExactPackageReference, ...]
    resolved_packages: tuple[ResolvedPackageComposition, ...]
    packages: tuple[HostPackageComposition, ...]
    entries: tuple[HostEntryComposition, ...]
    contributions: tuple[HostContributionComposition, ...]


@dataclass(frozen=True)
class HostCompositionPlanResult:
    """Atomic result: either one complete plan or stable diagnostics."""

    plan: HostCompositionPlan | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.plan is not None and not self.diagnostics


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8")


def _reference_key(reference: ExactPackageReference) -> tuple[bytes, bytes, bytes]:
    return (
        _utf8_key(reference.package_id),
        _utf8_key(reference.package_version),
        _utf8_key(reference.package_kind),
    )


def _diagnostic_sort_key(
    diagnostic: contracts.Diagnostic,
) -> tuple[str, str, str, str]:
    return (
        diagnostic.manifest_path,
        diagnostic.pointer,
        diagnostic.code,
        diagnostic.message,
    )


def _diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code=code,
        manifest_path=HOST_COMPOSITION_PLAN_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostCompositionPlanResult:
    return HostCompositionPlanResult(
        plan=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _integrity_record(value: dict[str, str]) -> IntegrityRecord:
    return IntegrityRecord(value["algorithm"], value["digest"])


def _exact_reference(value: dict[str, Any]) -> ExactPackageReference:
    return ExactPackageReference(
        package_id=value["id"],
        package_version=value["version"],
        package_kind=value["packageKind"],
    )


def _reference_data(reference: ExactPackageReference) -> dict[str, Any]:
    return {
        "id": reference.package_id,
        "version": reference.package_version,
        "packageKind": reference.package_kind,
    }


def _integrity_data(integrity: IntegrityRecord) -> dict[str, str]:
    return {"algorithm": integrity.algorithm, "digest": integrity.digest}


def host_composition_plan_to_data(plan: HostCompositionPlan) -> dict[str, Any]:
    """Return the fixed-field-order JSON-compatible representation of a plan."""

    return {
        "schema": HOST_COMPOSITION_PLAN_SCHEMA,
        "schemaVersion": HOST_COMPOSITION_PLAN_SCHEMA_VERSION,
        "inputs": {
            "engineApiVersion": plan.engine_api_version,
            "projectManifestIntegrity": _integrity_data(
                plan.project_manifest_integrity
            ),
            "lockedGraphIntegrity": _integrity_data(plan.locked_graph_integrity),
            "hostProfileIntegrity": _integrity_data(plan.host_profile_integrity),
        },
        "host": {
            "hostKind": plan.host_kind,
            "targetPlatform": plan.target_platform,
            "grantedCapabilities": list(plan.granted_capabilities),
        },
        "resolvedGraph": {
            "directPackages": [
                _reference_data(reference) for reference in plan.direct_packages
            ],
            "directFeatureSets": [
                _reference_data(reference) for reference in plan.direct_feature_sets
            ],
            "packages": [
                {
                    **_reference_data(package.package),
                    "dependencies": [
                        _reference_data(reference)
                        for reference in package.dependencies
                    ],
                }
                for package in plan.resolved_packages
            ],
        },
        "packages": [
            {
                **_reference_data(package.package),
                "dependencies": [
                    _reference_data(reference) for reference in package.dependencies
                ],
                "options": [
                    {
                        "id": option.option_id,
                        "type": option.option_type,
                        "affects": list(option.affects),
                        "value": option.value,
                    }
                    for option in package.options
                ],
                "modules": [
                    {
                        "id": module.module_id,
                        "role": module.role,
                        "shippingClass": module.shipping_class,
                        "requiredCapabilities": list(
                            module.required_capabilities
                        ),
                        "dependsOn": list(module.dependencies),
                    }
                    for module in package.modules
                ],
            }
            for package in plan.packages
        ],
        "entries": [
            {
                "dimension": entry.dimension,
                "packageId": entry.package_id,
                "packageVersion": entry.package_version,
                "moduleId": entry.module_id,
            }
            for entry in plan.entries
        ],
        "contributions": [
            {
                "packageId": contribution.package_id,
                "packageVersion": contribution.package_version,
                "id": contribution.contribution_id,
                "kind": contribution.contribution_kind,
                "ownerModuleId": contribution.owner_module_id,
            }
            for contribution in plan.contributions
        ],
    }


def render_host_composition_plan(plan: HostCompositionPlan) -> str:
    """Render canonical UTF-8-compatible JSON text with LF and a final newline."""

    return json.dumps(
        host_composition_plan_to_data(plan),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def _json_pointer(parts: Iterable[Any]) -> str:
    encoded = [str(part).replace("~", "~0").replace("/", "~1") for part in parts]
    return "/" + "/".join(encoded) if encoded else ""


def validate_host_composition_plan_data(
    plan: Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Validate the closed serialized shape of a Host Composition Plan."""

    diagnostics = [
        contracts.Diagnostic(
            code="plan.manifest.schema",
            manifest_path=HOST_COMPOSITION_PLAN_NAME,
            pointer=_json_pointer(error.absolute_path),
            message=error.message,
        )
        for error in validators.host_composition_plan.iter_errors(plan)
    ]
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _dependency_first_order(
    identities: Iterable[str],
    dependencies_for: Callable[[str], Iterable[str]],
    tie_key: Callable[[str], tuple[Any, ...]],
    cycle_code: str,
    cycle_label: str,
) -> tuple[tuple[str, ...] | None, list[contracts.Diagnostic]]:
    identity_set = set(identities)
    dependencies: dict[str, set[str]] = {}
    dependents: dict[str, set[str]] = {identity: set() for identity in identity_set}
    diagnostics: list[contracts.Diagnostic] = []

    for identity in identity_set:
        values = set(dependencies_for(identity))
        unknown = values - identity_set
        for missing in sorted(unknown, key=_utf8_key):
            diagnostics.append(
                _diagnostic(
                    "plan.graph.missing-dependency",
                    "/resolvedGraph/packages",
                    f"{cycle_label} '{identity}' depends on missing '{missing}'",
                )
            )
        values &= identity_set
        dependencies[identity] = values
        for dependency in values:
            dependents[dependency].add(identity)

    if diagnostics:
        return None, diagnostics

    ready = [identity for identity, values in dependencies.items() if not values]
    ordered: list[str] = []
    while ready:
        ready.sort(key=tie_key)
        identity = ready.pop(0)
        ordered.append(identity)
        for dependent in sorted(dependents[identity], key=tie_key):
            dependencies[dependent].discard(identity)
            if not dependencies[dependent] and dependent not in ready:
                ready.append(dependent)

    if len(ordered) != len(identity_set):
        remaining = sorted(identity_set - set(ordered), key=tie_key)
        return None, [
            _diagnostic(
                cycle_code,
                "/resolvedGraph/packages",
                f"{cycle_label} dependency cycle includes: {', '.join(remaining)}",
            )
        ]
    return tuple(ordered), []


def _verified_snapshot(
    verified_graph: Any,
    project: Any,
    validators: contracts.ContractValidators,
) -> tuple[
    dict[str, Any] | None,
    dict[str, Any] | None,
    tuple[PackageCandidate, ...],
    list[contracts.Diagnostic],
]:
    if not isinstance(verified_graph, LockedGraphVerificationResult):
        return None, None, (), [
            _diagnostic(
                "plan.input.unverified",
                "",
                "planner requires a LockedGraphVerificationResult",
            )
        ]
    try:
        upstream_diagnostics = tuple(verified_graph.diagnostics)
    except TypeError:
        return None, None, (), [
            _diagnostic(
                "plan.input.unverified",
                "",
                "locked verification diagnostics must be an immutable diagnostic snapshot",
            )
        ]
    if any(
        not isinstance(diagnostic, contracts.Diagnostic)
        for diagnostic in upstream_diagnostics
    ):
        return None, None, (), [
            _diagnostic(
                "plan.input.unverified",
                "",
                "locked verification diagnostics contain an unsupported value",
            )
        ]
    if upstream_diagnostics:
        return None, None, (), list(upstream_diagnostics)
    if verified_graph.lock is None:
        return None, None, (), [
            _diagnostic(
                "plan.input.unverified",
                "",
                "locked verification result did not contain a reusable graph",
            )
        ]

    try:
        candidates = tuple(verified_graph.selected_candidates)
    except TypeError:
        return None, None, (), [
            _diagnostic(
                "plan.input.incomplete",
                "/resolvedGraph/packages",
                "verified candidates must be an iterable snapshot",
            )
        ]
    if any(not isinstance(candidate, PackageCandidate) for candidate in candidates):
        return None, None, (), [
            _diagnostic(
                "plan.input.incomplete",
                "/resolvedGraph/packages",
                "every verified candidate must use the shared PackageCandidate contract",
            )
        ]
    if any(
        not isinstance(candidate.identity, str)
        or not isinstance(candidate.version, str)
        or not isinstance(candidate.package_kind, str)
        or not isinstance(candidate.source, dict)
        for candidate in candidates
    ):
        return None, None, (), [
            _diagnostic(
                "plan.input.incomplete",
                "/resolvedGraph/packages",
                "verified candidate metadata must be a complete package identity snapshot",
            )
        ]

    diagnostics = contracts.validate_manifest_data(
        verified_graph.lock,
        contracts.PACKAGE_LOCK_NAME,
        validators,
    )
    diagnostics.extend(
        contracts.validate_manifest_data(
            project,
            contracts.PROJECT_MANIFEST_NAME,
            validators,
        )
    )
    if diagnostics:
        return None, None, (), diagnostics

    normalized_lock = contracts.normalize_lock_manifest(verified_graph.lock)
    if verified_graph.lock != normalized_lock:
        return None, None, (), [
            _diagnostic(
                "plan.input.unverified",
                "/resolvedGraph/packages",
                "successful locked verification must provide a normalized exact graph",
            )
        ]
    normalized_project = contracts.normalize_project_manifest(project)

    ordered_candidates = tuple(
        sorted(
            candidates,
            key=lambda candidate: (
                _utf8_key(candidate.identity),
                _utf8_key(candidate.version),
                _utf8_key(candidate.package_kind),
            ),
        )
    )

    candidates_by_id: dict[str, list[PackageCandidate]] = {}
    for candidate in ordered_candidates:
        candidates_by_id.setdefault(candidate.identity, []).append(candidate)

    binding_diagnostics: list[contracts.Diagnostic] = []
    node_ids = {node["id"] for node in normalized_lock["nodes"]}
    extra_ids = set(candidates_by_id) - node_ids
    for identity in sorted(extra_ids, key=_utf8_key):
        binding_diagnostics.append(
            _diagnostic(
                "plan.input.incomplete",
                "/resolvedGraph/packages",
                f"verified candidates contain extra package '{identity}'",
            )
        )

    for node_index, node in enumerate(normalized_lock["nodes"]):
        identity = node["id"]
        matching = candidates_by_id.get(identity, [])
        if len(matching) != 1:
            binding_diagnostics.append(
                _diagnostic(
                    "plan.input.incomplete",
                    f"/resolvedGraph/packages/{node_index}",
                    (
                        f"locked package '{identity}' must have exactly one verified "
                        "candidate"
                    ),
                )
            )
            continue
        candidate = matching[0]
        if (
            candidate.version != node["version"]
            or candidate.package_kind != node["packageKind"]
            or candidate.source != node["source"]
            or (
                node["source"]["kind"] != "engine-distribution"
                and (
                    candidate.manifest_integrity != node["manifestIntegrity"]
                    or candidate.payload_integrity != node["payloadIntegrity"]
                )
            )
        ):
            binding_diagnostics.append(
                _diagnostic(
                    "plan.input.unverified",
                    f"/resolvedGraph/packages/{node_index}",
                    f"verified candidate evidence no longer matches '{identity}'",
                )
            )

    if binding_diagnostics:
        return None, None, (), binding_diagnostics

    diagnostics = contracts.validate_locked_result_data(
        normalized_lock,
        normalized_project,
        [candidate.manifest for candidate in ordered_candidates],
        validators,
    )
    if diagnostics:
        return None, None, (), diagnostics
    return normalized_lock, normalized_project, ordered_candidates, []


def _effective_options(
    manifest: dict[str, Any],
    overrides: dict[str, OptionValue],
) -> tuple[EffectivePackageOption, ...]:
    affect_order = {value: index for index, value in enumerate(OPTION_AFFECT_ORDER)}
    return tuple(
        EffectivePackageOption(
            option_id=option["id"],
            option_type=option["type"],
            affects=tuple(sorted(option["affects"], key=affect_order.__getitem__)),
            value=overrides.get(option["id"], option["default"]),
        )
        for option in sorted(manifest["options"], key=lambda value: _utf8_key(value["id"]))
    )


def plan_host_package_composition(
    verified_graph: Any,
    project: Any,
    host_profile: Any,
    validators: contracts.ContractValidators,
) -> HostCompositionPlanResult:
    """Build one canonical logical Host composition without resolving or writing."""

    lock, normalized_project, candidates, diagnostics = _verified_snapshot(
        verified_graph,
        project,
        validators,
    )
    if diagnostics:
        return _failure(diagnostics)
    assert lock is not None
    assert normalized_project is not None

    profile_diagnostics = contracts.validate_manifest_data(
        host_profile,
        contracts.HOST_PROFILE_NAME,
        validators,
    )
    if profile_diagnostics:
        return _failure(profile_diagnostics)
    normalized_profile = contracts.normalize_host_profile(host_profile)

    projection, projection_diagnostics = contracts.select_host_profile_data(
        lock,
        [candidate.manifest for candidate in candidates],
        normalized_profile,
        validators,
    )
    if projection_diagnostics:
        return _failure(projection_diagnostics)
    assert projection is not None

    nodes_by_id = {node["id"]: node for node in lock["nodes"]}
    package_order, order_diagnostics = _dependency_first_order(
        nodes_by_id,
        lambda identity: (
            dependency["id"] for dependency in nodes_by_id[identity]["dependencies"]
        ),
        lambda identity: (
            _utf8_key(identity),
            _utf8_key(nodes_by_id[identity]["version"]),
            _utf8_key(nodes_by_id[identity]["packageKind"]),
        ),
        "plan.graph.package-cycle",
        "package",
    )
    if order_diagnostics:
        return _failure(order_diagnostics)
    assert package_order is not None

    candidate_by_id = {candidate.identity: candidate for candidate in candidates}
    manifests_by_id = {
        identity: candidate_by_id[identity].manifest for identity in package_order
    }
    selected_modules_by_package: dict[str, set[str]] = {}
    for selection in projection.modules:
        selected_modules_by_package.setdefault(selection.package_id, set()).add(
            selection.module_id
        )

    option_overrides: dict[str, dict[str, OptionValue]] = {
        group["packageId"]: {
            option["id"]: option["value"] for option in group["values"]
        }
        for group in normalized_project["packageOptions"]
    }

    plan_diagnostics: list[contracts.Diagnostic] = []
    resolved_packages: list[ResolvedPackageComposition] = []
    host_packages: list[HostPackageComposition] = []
    module_order_by_package: dict[str, tuple[str, ...]] = {}

    for identity in package_order:
        node = nodes_by_id[identity]
        reference = _exact_reference(node)
        dependencies = tuple(
            sorted(
                (_exact_reference(value) for value in node["dependencies"]),
                key=_reference_key,
            )
        )
        resolved_packages.append(ResolvedPackageComposition(reference, dependencies))
        if node["packageKind"] != "installable-capability":
            continue

        manifest = manifests_by_id[identity]
        modules_by_id = {module["id"]: module for module in manifest["modules"]}
        selected = selected_modules_by_package.get(identity, set())
        module_order, module_diagnostics = _dependency_first_order(
            selected,
            lambda module_id: modules_by_id[module_id]["dependsOn"],
            lambda module_id: (_utf8_key(module_id),),
            "plan.graph.module-cycle",
            f"module in package '{identity}'",
        )
        if module_diagnostics:
            plan_diagnostics.extend(module_diagnostics)
            continue
        assert module_order is not None
        module_order_by_package[identity] = module_order
        modules = tuple(
            HostModuleComposition(
                module_id=module_id,
                role=modules_by_id[module_id]["role"],
                shipping_class=modules_by_id[module_id]["shippingClass"],
                required_capabilities=tuple(
                    sorted(
                        modules_by_id[module_id]["requiredCapabilities"],
                        key=_utf8_key,
                    )
                ),
                dependencies=tuple(
                    sorted(modules_by_id[module_id]["dependsOn"], key=_utf8_key)
                ),
            )
            for module_id in module_order
        )
        host_packages.append(
            HostPackageComposition(
                package=reference,
                dependencies=dependencies,
                options=_effective_options(
                    manifest,
                    option_overrides.get(identity, {}),
                ),
                modules=modules,
            )
        )

    if plan_diagnostics:
        return _failure(plan_diagnostics)

    installable_order = [
        identity
        for identity in package_order
        if nodes_by_id[identity]["packageKind"] == "installable-capability"
    ]
    entries: list[HostEntryComposition] = []
    for dimension in ENTRY_DIMENSION_ORDER:
        for identity in installable_order:
            manifest = manifests_by_id[identity]
            selected = selected_modules_by_package.get(identity, set())
            for module_id in sorted(
                manifest["entryModules"].get(dimension, []),
                key=_utf8_key,
            ):
                if module_id in selected:
                    entries.append(
                        HostEntryComposition(
                            dimension=dimension,
                            package_id=identity,
                            package_version=nodes_by_id[identity]["version"],
                            module_id=module_id,
                        )
                    )

    selected_contributions: dict[
        tuple[str, str], contracts.HostContributionSelection
    ] = {}
    for selection in projection.contributions:
        key = (selection.package_id, selection.contribution_id)
        if key in selected_contributions:
            plan_diagnostics.append(
                _diagnostic(
                    "plan.contribution.duplicate",
                    "/contributions",
                    (
                        f"Host projection selected contribution "
                        f"'{selection.contribution_id}' more than once for "
                        f"'{selection.package_id}'"
                    ),
                )
            )
            continue
        manifest = manifests_by_id.get(selection.package_id)
        declarations = (
            []
            if manifest is None or manifest["packageKind"] != "installable-capability"
            else [
                contribution
                for contribution in manifest["contributions"]
                if contribution["id"] == selection.contribution_id
            ]
        )
        declaration_matches = (
            len(declarations) == 1
            and declarations[0]["kind"] == selection.contribution_kind
            and declarations[0]["module"] == selection.owner_module_id
            and nodes_by_id[selection.package_id]["version"]
            == selection.package_version
        )
        owner_selected = selection.owner_module_id in selected_modules_by_package.get(
            selection.package_id,
            set(),
        )
        if not declaration_matches or not owner_selected:
            plan_diagnostics.append(
                _diagnostic(
                    "plan.contribution.owner-missing",
                    "/contributions",
                    (
                        f"contribution '{selection.contribution_id}' does not have a "
                        "matching selected owner module"
                    ),
                )
            )
            continue
        selected_contributions[key] = selection
    if plan_diagnostics:
        return _failure(plan_diagnostics)

    contributions: list[HostContributionComposition] = []
    for identity in installable_order:
        manifest = manifests_by_id[identity]
        module_order = module_order_by_package[identity]
        module_position = {
            module_id: index for index, module_id in enumerate(module_order)
        }
        compatible = [
            contribution
            for contribution in manifest["contributions"]
            if (identity, contribution["id"]) in selected_contributions
        ]
        compatible.sort(
            key=lambda contribution: (
                module_position[contribution["module"]],
                _utf8_key(contribution["id"]),
            )
        )
        contributions.extend(
            HostContributionComposition(
                package_id=identity,
                package_version=nodes_by_id[identity]["version"],
                contribution_id=contribution["id"],
                contribution_kind=contribution["kind"],
                owner_module_id=contribution["module"],
            )
            for contribution in compatible
        )

    lock_bytes = contracts.render_normalized_lock_manifest(lock).encode("utf-8")
    profile_bytes = contracts.render_normalized_host_profile(normalized_profile).encode(
        "utf-8"
    )
    plan = HostCompositionPlan(
        engine_api_version=lock["inputs"]["engine"]["engineApiVersion"],
        project_manifest_integrity=_integrity_record(
            lock["inputs"]["projectManifestIntegrity"]
        ),
        locked_graph_integrity=_integrity_record(
            contracts.compute_bytes_integrity(lock_bytes)
        ),
        host_profile_integrity=_integrity_record(
            contracts.compute_bytes_integrity(profile_bytes)
        ),
        host_kind=normalized_profile["hostKind"],
        target_platform=normalized_profile["targetPlatform"],
        granted_capabilities=tuple(normalized_profile["grantedCapabilities"]),
        direct_packages=tuple(
            sorted(
                (_exact_reference(value) for value in lock["roots"]["directPackages"]),
                key=_reference_key,
            )
        ),
        direct_feature_sets=tuple(
            sorted(
                (
                    _exact_reference(value)
                    for value in lock["roots"]["directFeatureSets"]
                ),
                key=_reference_key,
            )
        ),
        resolved_packages=tuple(resolved_packages),
        packages=tuple(host_packages),
        entries=tuple(entries),
        contributions=tuple(contributions),
    )
    output_diagnostics = validate_host_composition_plan_data(
        host_composition_plan_to_data(plan),
        validators,
    )
    if output_diagnostics:
        return _failure(output_diagnostics)
    return HostCompositionPlanResult(plan=plan, diagnostics=())
