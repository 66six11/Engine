"""Deterministic Source Build Plan v1 planning from verified package evidence."""

from __future__ import annotations

import json
from dataclasses import dataclass, replace
from typing import Any, Iterable, TypeAlias

from tools import check_package_contracts as contracts
from tools import host_package_composition as composition
from tools.cmake_file_api import (
    CMakeCodemodelSnapshot,
    CMakeGeneratorEvidence,
    CMakeToolchainEvidence,
    cmake_codemodel_snapshot_to_data,
    compute_cmake_codemodel_snapshot_integrity,
    validate_cmake_codemodel_snapshot,
)
from tools.package_candidates import PackageCandidate
from tools.package_lock_verification import LockedGraphVerificationResult


SOURCE_BUILD_PLAN_NAME = "asharia.source-build-plan.json"
SOURCE_BUILD_PLAN_SCHEMA = "com.asharia.source-build-plan"
SOURCE_BUILD_PLAN_SCHEMA_VERSION = 1
BUILDABLE_TARGET_TYPES = frozenset(
    {
        "EXECUTABLE",
        "STATIC_LIBRARY",
        "SHARED_LIBRARY",
        "MODULE_LIBRARY",
        "OBJECT_LIBRARY",
        "UTILITY",
    }
)
OPTION_AFFECT_ORDER = ("build", "activation", "cook")

OptionValue: TypeAlias = bool | int | str


@dataclass(frozen=True, order=True)
class IntegrityRecord:
    """Immutable structured integrity evidence."""

    algorithm: str
    digest: str


@dataclass(frozen=True, order=True)
class BuildTargetReference:
    """One canonical CMake build target name and expected type."""

    name: str
    target_type: str


@dataclass(frozen=True)
class SourceModuleBuildBinding:
    """One selected logical module mapped to source boundaries and build roots."""

    module_id: str
    source_boundaries: tuple[str, ...]
    build_kind: str
    targets: tuple[BuildTargetReference, ...]


@dataclass(frozen=True)
class SourcePackageBuildBinding:
    """Selected module bindings for one exact installable package."""

    package_id: str
    package_version: str
    modules: tuple[SourceModuleBuildBinding, ...]


@dataclass(frozen=True, order=True)
class TargetClosureEvidence:
    """One configured CMake target and its reported build dependencies."""

    name: str
    target_type: str
    dependencies: tuple[str, ...]


@dataclass(frozen=True)
class SourceBuildOption:
    """One Host-effective option whose affects set includes build."""

    package_id: str
    option_id: str
    option_type: str
    affects: tuple[str, ...]
    value: OptionValue


@dataclass(frozen=True)
class SourceBuildInputs:
    """Canonical fingerprints for every independent plan authority."""

    host_composition_integrity: IntegrityRecord
    descriptor_set_integrity: IntegrityRecord
    topology_integrity: IntegrityRecord
    codemodel_integrity: IntegrityRecord
    configuration_integrity: IntegrityRecord


@dataclass(frozen=True)
class SourceBuildPlan:
    """Canonical verified build-root handoff for one Host composition."""

    inputs: SourceBuildInputs
    host_kind: str
    target_platform: str
    configuration: str
    generator: CMakeGeneratorEvidence
    toolchain: CMakeToolchainEvidence
    packages: tuple[SourcePackageBuildBinding, ...]
    build_roots: tuple[BuildTargetReference, ...]
    target_closure: tuple[TargetClosureEvidence, ...]
    build_options: tuple[SourceBuildOption, ...]
    integrity: IntegrityRecord


@dataclass(frozen=True)
class SourceBuildPlanResult:
    """Atomic result: either one complete plan or stable diagnostics."""

    plan: SourceBuildPlan | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.plan is not None and not self.diagnostics


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8")


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
        manifest_path=SOURCE_BUILD_PLAN_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(diagnostics: Iterable[contracts.Diagnostic]) -> SourceBuildPlanResult:
    return SourceBuildPlanResult(
        plan=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _integrity_record(value: dict[str, str]) -> IntegrityRecord:
    return IntegrityRecord(value["algorithm"], value["digest"])


def _integrity_data(value: IntegrityRecord) -> dict[str, str]:
    return {"algorithm": value.algorithm, "digest": value.digest}


def _generator_data(generator: CMakeGeneratorEvidence) -> dict[str, Any]:
    return {"name": generator.name, "multiConfig": generator.multi_config}


def _toolchain_data(toolchain: CMakeToolchainEvidence) -> dict[str, str]:
    return {
        "compilerId": toolchain.compiler_id,
        "compilerVersion": toolchain.compiler_version,
        "targetSystem": toolchain.target_system,
        "targetArchitecture": toolchain.target_architecture,
    }


def _target_reference_data(target: BuildTargetReference) -> dict[str, str]:
    return {"name": target.name, "type": target.target_type}


def _plan_payload_data(plan: SourceBuildPlan) -> dict[str, Any]:
    return {
        "schema": SOURCE_BUILD_PLAN_SCHEMA,
        "schemaVersion": SOURCE_BUILD_PLAN_SCHEMA_VERSION,
        "inputs": {
            "hostCompositionIntegrity": _integrity_data(
                plan.inputs.host_composition_integrity
            ),
            "descriptorSetIntegrity": _integrity_data(
                plan.inputs.descriptor_set_integrity
            ),
            "topologyIntegrity": _integrity_data(plan.inputs.topology_integrity),
            "codemodelIntegrity": _integrity_data(plan.inputs.codemodel_integrity),
            "configurationIntegrity": _integrity_data(
                plan.inputs.configuration_integrity
            ),
        },
        "host": {
            "hostKind": plan.host_kind,
            "targetPlatform": plan.target_platform,
        },
        "configuration": {
            "name": plan.configuration,
            "generator": _generator_data(plan.generator),
            "toolchain": _toolchain_data(plan.toolchain),
        },
        "packages": [
            {
                "id": package.package_id,
                "version": package.package_version,
                "modules": [
                    {
                        "moduleId": module.module_id,
                        "sourceBoundaries": list(module.source_boundaries),
                        "build": (
                            {"kind": "no-build"}
                            if module.build_kind == "no-build"
                            else {
                                "kind": "target-roots",
                                "targets": [
                                    _target_reference_data(target)
                                    for target in module.targets
                                ],
                            }
                        ),
                    }
                    for module in package.modules
                ],
            }
            for package in plan.packages
        ],
        "buildRoots": [
            _target_reference_data(target) for target in plan.build_roots
        ],
        "targetClosure": [
            {
                "name": target.name,
                "type": target.target_type,
                "dependencies": list(target.dependencies),
            }
            for target in plan.target_closure
        ],
        "buildOptions": [
            {
                "packageId": option.package_id,
                "id": option.option_id,
                "type": option.option_type,
                "affects": list(option.affects),
                "value": option.value,
            }
            for option in plan.build_options
        ],
    }


def _data_integrity(value: Any) -> dict[str, str]:
    canonical = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return contracts.compute_bytes_integrity(canonical)


def source_build_plan_to_data(plan: SourceBuildPlan) -> dict[str, Any]:
    """Return the fixed-field-order JSON-compatible representation of a plan."""

    return {**_plan_payload_data(plan), "integrity": _integrity_data(plan.integrity)}


def render_source_build_plan(plan: SourceBuildPlan) -> str:
    """Render canonical Source Build Plan JSON with LF and a final newline."""

    return json.dumps(
        source_build_plan_to_data(plan),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def compute_source_build_plan_integrity(plan: SourceBuildPlan) -> dict[str, str]:
    """Hash all canonical plan fields except the self-describing integrity field."""

    return _data_integrity(_plan_payload_data(plan))


def validate_source_build_plan_data(
    plan: Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Validate the closed plan shape and its self-integrity."""

    data = source_build_plan_to_data(plan) if isinstance(plan, SourceBuildPlan) else plan
    diagnostics = contracts.validate_manifest_data(
        data,
        SOURCE_BUILD_PLAN_NAME,
        validators,
    )
    if diagnostics or not isinstance(data, dict):
        return diagnostics
    payload = {key: value for key, value in data.items() if key != "integrity"}
    if data["integrity"] != _data_integrity(payload):
        diagnostics.append(
            _diagnostic(
                "build.plan.integrity-mismatch",
                "/integrity",
                "Source Build Plan integrity does not match canonical plan fields",
            )
        )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _candidate_build_descriptor_diagnostics(
    candidate: PackageCandidate,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    descriptor_path = f"candidate/{candidate.identity}/{contracts.PACKAGE_SOURCE_BUILD_NAME}"
    values = (
        candidate.build_descriptor,
        candidate.build_descriptor_integrity,
        candidate.build_descriptor_bytes,
    )
    present_count = sum(value is not None for value in values)
    if present_count == 0:
        return [
            _diagnostic(
                "build.descriptor.missing",
                "/packages",
                f"selected source package '{candidate.identity}' has no build descriptor",
            )
        ]
    if present_count != 3:
        return [
            _diagnostic(
                "build.descriptor.incomplete",
                "/packages",
                f"build descriptor snapshot for '{candidate.identity}' is incomplete",
            )
        ]
    if (
        not isinstance(candidate.build_descriptor, dict)
        or not isinstance(candidate.build_descriptor_integrity, dict)
        or not isinstance(candidate.build_descriptor_bytes, bytes)
    ):
        return [
            _diagnostic(
                "build.descriptor.incomplete",
                "/packages",
                f"build descriptor snapshot for '{candidate.identity}' has invalid types",
            )
        ]

    diagnostics: list[contracts.Diagnostic] = []
    if contracts.compute_bytes_integrity(
        candidate.build_descriptor_bytes
    ) != candidate.build_descriptor_integrity:
        diagnostics.append(
            _diagnostic(
                "build.descriptor.integrity-mismatch",
                "/inputs/descriptorSetIntegrity",
                f"build descriptor bytes changed for '{candidate.identity}'",
            )
        )
    try:
        parsed = json.loads(candidate.build_descriptor_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        parsed = None
    if parsed != candidate.build_descriptor:
        diagnostics.append(
            _diagnostic(
                "build.descriptor.snapshot-mismatch",
                "/inputs/descriptorSetIntegrity",
                f"captured descriptor data and bytes differ for '{candidate.identity}'",
            )
        )
    diagnostics.extend(
        contracts.validate_package_source_build_binding(
            candidate.build_descriptor,
            candidate.manifest,
            validators,
            descriptor_path=descriptor_path,
            manifest_path=f"candidate/{candidate.identity}/{contracts.PACKAGE_MANIFEST_NAME}",
        )
    )
    return diagnostics


def _verify_planner_inputs(
    host_plan: Any,
    verified_graph: Any,
    topology_snapshot: Any,
    codemodel_snapshot: Any,
    validators: contracts.ContractValidators,
) -> tuple[
    composition.HostCompositionPlan | None,
    dict[str, Any] | None,
    tuple[PackageCandidate, ...],
    dict[str, Any] | None,
    CMakeCodemodelSnapshot | None,
    list[contracts.Diagnostic],
]:
    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(host_plan, composition.HostCompositionPlan):
        diagnostics.append(
            _diagnostic(
                "build.input.host-composition-invalid",
                "",
                "planner requires a HostCompositionPlan",
            )
        )
    else:
        diagnostics.extend(
            composition.validate_host_composition_plan_data(
                composition.host_composition_plan_to_data(host_plan),
                validators,
            )
        )

    if not isinstance(verified_graph, LockedGraphVerificationResult):
        diagnostics.append(
            _diagnostic(
                "build.input.unverified",
                "",
                "planner requires a LockedGraphVerificationResult",
            )
        )
        lock = None
        candidates: tuple[PackageCandidate, ...] = ()
    elif not verified_graph.succeeded:
        diagnostics.append(
            _diagnostic(
                "build.input.unverified",
                "",
                "locked package graph must be successfully verified",
            )
        )
        lock = None
        candidates = ()
    else:
        lock = verified_graph.lock
        try:
            candidates = tuple(verified_graph.selected_candidates)
        except TypeError:
            candidates = ()
            diagnostics.append(
                _diagnostic(
                    "build.input.unverified",
                    "/packages",
                    "verified candidate snapshot must be iterable",
                )
            )
        if any(not isinstance(candidate, PackageCandidate) for candidate in candidates):
            diagnostics.append(
                _diagnostic(
                    "build.input.unverified",
                    "/packages",
                    "verified candidate snapshot contains unsupported values",
                )
            )

    normalized_topology: dict[str, Any] | None = None
    topology_diagnostics = contracts.validate_manifest_data(
        topology_snapshot,
        "source-topology.json",
        validators,
    )
    diagnostics.extend(topology_diagnostics)
    if not topology_diagnostics:
        assert isinstance(topology_snapshot, dict)
        normalized_topology = contracts.normalize_source_topology_snapshot(
            topology_snapshot
        )

    if not isinstance(codemodel_snapshot, CMakeCodemodelSnapshot):
        diagnostics.append(
            _diagnostic(
                "build.input.codemodel-invalid",
                "/configuration",
                "planner requires a CMakeCodemodelSnapshot",
            )
        )
        normalized_codemodel = None
    else:
        codemodel_diagnostics = validate_cmake_codemodel_snapshot(
            codemodel_snapshot,
            validators,
        )
        diagnostics.extend(codemodel_diagnostics)
        normalized_codemodel = codemodel_snapshot if not codemodel_diagnostics else None

    if diagnostics:
        return None, None, (), None, None, diagnostics
    assert isinstance(host_plan, composition.HostCompositionPlan)
    assert isinstance(lock, dict)
    assert normalized_topology is not None
    assert normalized_codemodel is not None

    lock_diagnostics = contracts.validate_manifest_data(
        lock,
        contracts.PACKAGE_LOCK_NAME,
        validators,
    )
    if lock_diagnostics:
        return None, None, (), None, None, lock_diagnostics
    normalized_lock = contracts.normalize_lock_manifest(lock)
    if normalized_lock != lock:
        diagnostics.append(
            _diagnostic(
                "build.input.unverified",
                "/inputs/hostCompositionIntegrity",
                "successful locked verification must provide a normalized graph",
            )
        )
    expected_lock_integrity = contracts.compute_bytes_integrity(
        contracts.render_normalized_lock_manifest(normalized_lock).encode("utf-8")
    )
    if {
        "algorithm": host_plan.locked_graph_integrity.algorithm,
        "digest": host_plan.locked_graph_integrity.digest,
    } != expected_lock_integrity:
        diagnostics.append(
            _diagnostic(
                "build.input.host-composition-stale",
                "/inputs/hostCompositionIntegrity",
                "Host Composition Plan does not match the verified locked graph",
            )
        )

    candidates_by_id: dict[str, list[PackageCandidate]] = {}
    for candidate in candidates:
        candidates_by_id.setdefault(candidate.identity, []).append(candidate)
    nodes_by_id = {node["id"]: node for node in normalized_lock["nodes"]}
    for identity, node in nodes_by_id.items():
        matching = candidates_by_id.get(identity, [])
        if len(matching) != 1:
            diagnostics.append(
                _diagnostic(
                    "build.input.unverified",
                    "/packages",
                    f"locked package '{identity}' must have exactly one verified candidate",
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
            diagnostics.append(
                _diagnostic(
                    "build.input.unverified",
                    "/packages",
                    f"candidate evidence no longer matches locked package '{identity}'",
                )
            )
    extra_candidates = set(candidates_by_id) - set(nodes_by_id)
    for identity in sorted(extra_candidates, key=_utf8_key):
        diagnostics.append(
            _diagnostic(
                "build.input.unverified",
                "/packages",
                f"verified candidate snapshot contains extra package '{identity}'",
            )
        )

    for package in host_plan.packages:
        matching = candidates_by_id.get(package.package.package_id, [])
        if len(matching) != 1:
            continue
        candidate = matching[0]
        if (
            candidate.version != package.package.package_version
            or candidate.package_kind != "installable-capability"
        ):
            diagnostics.append(
                _diagnostic(
                    "build.input.host-composition-stale",
                    "/packages",
                    f"Host package '{package.package.package_id}' is not exact",
                )
            )
            continue
        modules_by_id = {
            module["id"]: module for module in candidate.manifest.get("modules", [])
        }
        for module in package.modules:
            author_module = modules_by_id.get(module.module_id)
            if author_module is None:
                diagnostics.append(
                    _diagnostic(
                        "build.input.host-composition-stale",
                        "/packages",
                        (
                            f"Host module '{package.package.package_id}:"
                            f"{module.module_id}' is absent from its author manifest"
                        ),
                    )
                )

    if diagnostics:
        return None, None, (), None, None, diagnostics
    return (
        host_plan,
        normalized_lock,
        tuple(candidates),
        normalized_topology,
        normalized_codemodel,
        [],
    )


def _descriptor_set_integrity(
    candidates: Iterable[PackageCandidate],
) -> dict[str, str]:
    records = [
        {
            "id": candidate.identity,
            "version": candidate.version,
            "descriptor": contracts.normalize_package_source_build_descriptor(
                candidate.build_descriptor
            ),
        }
        for candidate in sorted(
            candidates,
            key=lambda value: (
                _utf8_key(value.identity),
                _utf8_key(value.version),
            ),
        )
    ]
    return _data_integrity(records)


def _configuration_integrity(snapshot: CMakeCodemodelSnapshot) -> dict[str, str]:
    return _data_integrity(
        {
            "name": snapshot.configuration,
            "generator": _generator_data(snapshot.generator),
            "toolchain": _toolchain_data(snapshot.toolchain),
        }
    )


def plan_source_build(
    host_plan: Any,
    verified_graph: Any,
    topology_snapshot: Any,
    codemodel_snapshot: Any,
    validators: contracts.ContractValidators,
) -> SourceBuildPlanResult:
    """Build one canonical verified target-root plan without IO or execution."""

    (
        verified_host_plan,
        _,
        candidates,
        normalized_topology,
        verified_codemodel,
        diagnostics,
    ) = _verify_planner_inputs(
        host_plan,
        verified_graph,
        topology_snapshot,
        codemodel_snapshot,
        validators,
    )
    if diagnostics:
        return _failure(diagnostics)
    assert verified_host_plan is not None
    assert normalized_topology is not None
    assert verified_codemodel is not None

    candidates_by_id = {candidate.identity: candidate for candidate in candidates}
    boundaries_by_id = {
        package["name"]: package for package in normalized_topology["packages"]
    }
    target_owners: dict[str, tuple[dict[str, Any], dict[str, Any]]] = {}
    for boundary in normalized_topology["packages"]:
        for target in boundary["targets"]:
            target_owners[target["name"]] = (boundary, target)
    cmake_targets = {target.name: target for target in verified_codemodel.targets}

    package_bindings: list[SourcePackageBuildBinding] = []
    root_targets: dict[str, BuildTargetReference] = {}
    descriptor_candidates: list[PackageCandidate] = []
    build_options: list[SourceBuildOption] = []
    plan_diagnostics: list[contracts.Diagnostic] = []
    affect_order = {value: index for index, value in enumerate(OPTION_AFFECT_ORDER)}

    for host_package in verified_host_plan.packages:
        package_id = host_package.package.package_id
        candidate = candidates_by_id[package_id]
        module_bindings: list[SourceModuleBuildBinding] = []
        descriptor_modules: dict[str, dict[str, Any]] = {}
        if host_package.modules:
            descriptor_diagnostics = _candidate_build_descriptor_diagnostics(
                candidate,
                validators,
            )
            plan_diagnostics.extend(descriptor_diagnostics)
            if not descriptor_diagnostics:
                assert candidate.build_descriptor is not None
                normalized_descriptor = (
                    contracts.normalize_package_source_build_descriptor(
                        candidate.build_descriptor
                    )
                )
                descriptor_modules = {
                    module["moduleId"]: module
                    for module in normalized_descriptor["modules"]
                }
                descriptor_candidates.append(candidate)

        for host_module in host_package.modules:
            descriptor_module = descriptor_modules.get(host_module.module_id)
            if descriptor_module is None:
                if not any(
                    diagnostic.code.startswith("build.descriptor")
                    and package_id in diagnostic.message
                    for diagnostic in plan_diagnostics
                ):
                    plan_diagnostics.append(
                        _diagnostic(
                            "build.descriptor.missing-module",
                            "/packages",
                            (
                                f"selected logical module '{package_id}:"
                                f"{host_module.module_id}' has no build binding"
                            ),
                        )
                    )
                continue

            source_boundaries = tuple(descriptor_module["sourceBoundaries"])
            for boundary_id in source_boundaries:
                boundary = boundaries_by_id.get(boundary_id)
                if boundary is None:
                    plan_diagnostics.append(
                        _diagnostic(
                            "build.descriptor.unknown-boundary",
                            "/packages",
                            f"source boundary '{boundary_id}' is absent from topology",
                        )
                    )
                elif boundary["plannedOwnershipRoot"] != package_id:
                    plan_diagnostics.append(
                        _diagnostic(
                            "build.descriptor.boundary-owner-mismatch",
                            "/packages",
                            (
                                f"source boundary '{boundary_id}' belongs to planned root "
                                f"'{boundary['plannedOwnershipRoot']}', not '{package_id}'"
                            ),
                        )
                    )

            build = descriptor_module["build"]
            module_targets: list[BuildTargetReference] = []
            if build["kind"] == "target-roots":
                for descriptor_target in build["targets"]:
                    target_name = descriptor_target["name"]
                    expected_type = descriptor_target["type"]
                    cmake_target = cmake_targets.get(target_name)
                    owner = target_owners.get(target_name)
                    if cmake_target is None:
                        plan_diagnostics.append(
                            _diagnostic(
                                "build.target.missing",
                                "/buildRoots",
                                f"configured CMake target '{target_name}' is absent",
                            )
                        )
                        continue
                    if cmake_target.target_type != expected_type:
                        plan_diagnostics.append(
                            _diagnostic(
                                "build.target.type-mismatch",
                                "/buildRoots",
                                (
                                    f"target '{target_name}' is '{cmake_target.target_type}', "
                                    f"not descriptor type '{expected_type}'"
                                ),
                            )
                        )
                    if cmake_target.target_type not in BUILDABLE_TARGET_TYPES:
                        plan_diagnostics.append(
                            _diagnostic(
                                "build.target.non-buildable",
                                "/buildRoots",
                                f"target '{target_name}' cannot be used as a build root",
                            )
                        )
                    if owner is None or owner[0]["name"] not in source_boundaries:
                        plan_diagnostics.append(
                            _diagnostic(
                                "build.target.owner-mismatch",
                                "/buildRoots",
                                (
                                    f"target '{target_name}' is not owned by a bound "
                                    "source boundary"
                                ),
                            )
                        )
                    elif owner[1]["test"] or owner[1]["role"] == "test":
                        plan_diagnostics.append(
                            _diagnostic(
                                "build.target.test-root",
                                "/buildRoots",
                                f"test target '{target_name}' cannot be a package build root",
                            )
                        )
                    reference = BuildTargetReference(target_name, expected_type)
                    module_targets.append(reference)
                    root_targets[target_name] = reference

            module_bindings.append(
                SourceModuleBuildBinding(
                    module_id=host_module.module_id,
                    source_boundaries=source_boundaries,
                    build_kind=build["kind"],
                    targets=tuple(
                        sorted(module_targets, key=lambda value: _utf8_key(value.name))
                    ),
                )
            )

        package_bindings.append(
            SourcePackageBuildBinding(
                package_id=package_id,
                package_version=host_package.package.package_version,
                modules=tuple(module_bindings),
            )
        )
        for option in host_package.options:
            if "build" not in option.affects:
                continue
            build_options.append(
                SourceBuildOption(
                    package_id=package_id,
                    option_id=option.option_id,
                    option_type=option.option_type,
                    affects=tuple(
                        sorted(option.affects, key=affect_order.__getitem__)
                    ),
                    value=option.value,
                )
            )

    if plan_diagnostics:
        return _failure(plan_diagnostics)

    closure_names: set[str] = set()
    pending = list(root_targets)
    while pending:
        target_name = pending.pop()
        if target_name in closure_names:
            continue
        target = cmake_targets.get(target_name)
        if target is None:
            return _failure(
                [
                    _diagnostic(
                        "build.target.dangling-dependency",
                        "/targetClosure",
                        f"target closure references missing '{target_name}'",
                    )
                ]
            )
        closure_names.add(target_name)
        pending.extend(target.dependencies)

    target_closure = tuple(
        TargetClosureEvidence(
            name=target.name,
            target_type=target.target_type,
            dependencies=tuple(sorted(target.dependencies, key=_utf8_key)),
        )
        for target in sorted(
            (cmake_targets[name] for name in closure_names),
            key=lambda value: _utf8_key(value.name),
        )
    )

    inputs = SourceBuildInputs(
        host_composition_integrity=_integrity_record(
            contracts.compute_bytes_integrity(
                composition.render_host_composition_plan(
                    verified_host_plan
                ).encode("utf-8")
            )
        ),
        descriptor_set_integrity=_integrity_record(
            _descriptor_set_integrity(descriptor_candidates)
        ),
        topology_integrity=_integrity_record(
            contracts.compute_bytes_integrity(
                contracts.render_normalized_source_topology_snapshot(
                    normalized_topology
                ).encode("utf-8")
            )
        ),
        codemodel_integrity=_integrity_record(
            compute_cmake_codemodel_snapshot_integrity(verified_codemodel)
        ),
        configuration_integrity=_integrity_record(
            _configuration_integrity(verified_codemodel)
        ),
    )
    plan = SourceBuildPlan(
        inputs=inputs,
        host_kind=verified_host_plan.host_kind,
        target_platform=verified_host_plan.target_platform,
        configuration=verified_codemodel.configuration,
        generator=verified_codemodel.generator,
        toolchain=verified_codemodel.toolchain,
        packages=tuple(package_bindings),
        build_roots=tuple(
            root_targets[name]
            for name in sorted(root_targets, key=_utf8_key)
        ),
        target_closure=target_closure,
        build_options=tuple(build_options),
        integrity=IntegrityRecord("sha256", "0" * 64),
    )
    plan = replace(plan, integrity=_integrity_record(compute_source_build_plan_integrity(plan)))
    output_diagnostics = validate_source_build_plan_data(plan, validators)
    if output_diagnostics:
        return _failure(output_diagnostics)
    return SourceBuildPlanResult(plan=plan, diagnostics=())
