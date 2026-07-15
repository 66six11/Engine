"""Verify selected static native factory providers without generating C++."""

from __future__ import annotations

import json
from dataclasses import dataclass, replace
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_activation_blueprint as activation
from tools import source_build_plan
from tools.effective_session import EffectiveSessionPlan, validate_ready_effective_session
from tools.package_candidates import PackageCandidate


STATIC_FACTORY_PROVIDER_BINDING_PLAN_NAME = (
    "asharia.static-factory-provider-binding-plan.json"
)
STATIC_FACTORY_PROVIDER_BINDING_PLAN_SCHEMA = (
    "com.asharia.static-factory-provider-binding-plan"
)
STATIC_FACTORY_PROVIDER_BINDING_PLAN_SCHEMA_VERSION = 1
STATIC_FACTORY_PROVIDER_API = "asharia-static-factory-provider-v1"


@dataclass(frozen=True, order=True)
class IntegrityRecord:
    """Immutable SHA-256 evidence."""

    algorithm: str
    digest: str


@dataclass(frozen=True, order=True)
class ProviderTarget:
    """One selected static CMake target."""

    name: str
    target_type: str


@dataclass(frozen=True, order=True)
class ProviderEntryPoint:
    """One public header and directly referenced C++ function."""

    header: str
    function: str


@dataclass(frozen=True)
class StaticFactoryProvider:
    """One provider call and the exact selected factories it must register."""

    package_id: str
    package_version: str
    module_id: str
    target: ProviderTarget
    entry_point: ProviderEntryPoint
    factory_ids: tuple[str, ...]


@dataclass(frozen=True)
class StaticFactoryProviderBindingInputs:
    """Fingerprints for every independent handoff authority."""

    effective_session_integrity: IntegrityRecord
    source_build_plan_integrity: IntegrityRecord
    host_activation_blueprint_integrity: IntegrityRecord
    binding_declaration_set_integrity: IntegrityRecord


@dataclass(frozen=True)
class StaticFactoryProviderBindingPlan:
    """Canonical pre-generation provider handoff; not a lock or receipt."""

    inputs: StaticFactoryProviderBindingInputs
    engine_generation_id: str
    host_kind: str
    target_platform: str
    providers: tuple[StaticFactoryProvider, ...]
    integrity: IntegrityRecord


@dataclass(frozen=True)
class StaticFactoryProviderBindingResult:
    """Atomic result: one complete binding plan or stable diagnostics."""

    plan: StaticFactoryProviderBindingPlan | None
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
        manifest_path=STATIC_FACTORY_PROVIDER_BINDING_PLAN_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> StaticFactoryProviderBindingResult:
    unique = {
        (
            value.manifest_path,
            value.pointer,
            value.code,
            value.message,
        ): value
        for value in diagnostics
    }
    return StaticFactoryProviderBindingResult(
        plan=None,
        diagnostics=tuple(sorted(unique.values(), key=_diagnostic_sort_key)),
    )


def _integrity_record(value: dict[str, str]) -> IntegrityRecord:
    return IntegrityRecord(value["algorithm"], value["digest"])


def _integrity_data(value: IntegrityRecord) -> dict[str, str]:
    return {"algorithm": value.algorithm, "digest": value.digest}


def _canonical_integrity(value: Any) -> IntegrityRecord:
    data = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return _integrity_record(contracts.compute_bytes_integrity(data))


def _provider_sort_key(
    provider: StaticFactoryProvider,
) -> tuple[bytes, bytes, bytes, bytes, bytes, bytes]:
    return (
        _utf8_key(provider.package_id),
        _utf8_key(provider.package_version),
        _utf8_key(provider.module_id),
        _utf8_key(provider.target.name),
        _utf8_key(provider.entry_point.header),
        _utf8_key(provider.entry_point.function),
    )


def _plan_payload_data(plan: StaticFactoryProviderBindingPlan) -> dict[str, Any]:
    return {
        "schema": STATIC_FACTORY_PROVIDER_BINDING_PLAN_SCHEMA,
        "schemaVersion": STATIC_FACTORY_PROVIDER_BINDING_PLAN_SCHEMA_VERSION,
        "inputs": {
            "effectiveSessionIntegrity": _integrity_data(
                plan.inputs.effective_session_integrity
            ),
            "sourceBuildPlanIntegrity": _integrity_data(
                plan.inputs.source_build_plan_integrity
            ),
            "hostActivationBlueprintIntegrity": _integrity_data(
                plan.inputs.host_activation_blueprint_integrity
            ),
            "bindingDeclarationSetIntegrity": _integrity_data(
                plan.inputs.binding_declaration_set_integrity
            ),
        },
        "host": {
            "engineGenerationId": plan.engine_generation_id,
            "hostKind": plan.host_kind,
            "targetPlatform": plan.target_platform,
        },
        "providerApi": STATIC_FACTORY_PROVIDER_API,
        "providers": [
            {
                "packageId": provider.package_id,
                "packageVersion": provider.package_version,
                "moduleId": provider.module_id,
                "target": {
                    "name": provider.target.name,
                    "type": provider.target.target_type,
                },
                "entryPoint": {
                    "header": provider.entry_point.header,
                    "function": provider.entry_point.function,
                },
                "factoryIds": list(provider.factory_ids),
            }
            for provider in plan.providers
        ],
    }


def static_factory_provider_binding_plan_to_data(
    plan: StaticFactoryProviderBindingPlan,
) -> dict[str, Any]:
    return {
        **_plan_payload_data(plan),
        "integrity": _integrity_data(plan.integrity),
    }


def render_static_factory_provider_binding_plan(
    plan: StaticFactoryProviderBindingPlan,
) -> str:
    return json.dumps(
        static_factory_provider_binding_plan_to_data(plan),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def compute_static_factory_provider_binding_plan_integrity(
    plan: StaticFactoryProviderBindingPlan,
) -> dict[str, str]:
    return _integrity_data(_canonical_integrity(_plan_payload_data(plan)))


def validate_static_factory_provider_binding_plan_data(
    plan: StaticFactoryProviderBindingPlan | Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    data = (
        static_factory_provider_binding_plan_to_data(plan)
        if isinstance(plan, StaticFactoryProviderBindingPlan)
        else plan
    )
    diagnostics = contracts.validate_manifest_data(
        data,
        STATIC_FACTORY_PROVIDER_BINDING_PLAN_NAME,
        validators,
    )
    if diagnostics:
        return diagnostics

    provider_keys: set[tuple[str, str, str, str, str, str]] = set()
    factory_keys: set[tuple[str, str, str, str]] = set()
    previous_provider_key: tuple[bytes, ...] | None = None
    for provider_index, provider in enumerate(data["providers"]):
        provider_key = (
            provider["packageId"],
            provider["packageVersion"],
            provider["moduleId"],
            provider["target"]["name"],
            provider["entryPoint"]["header"],
            provider["entryPoint"]["function"],
        )
        if provider_key in provider_keys:
            diagnostics.append(
                _diagnostic(
                    "factory.binding-plan.duplicate-provider",
                    f"/providers/{provider_index}",
                    "provider entry point appears more than once",
                )
            )
        provider_keys.add(provider_key)
        canonical_provider_key = tuple(_utf8_key(value) for value in provider_key)
        if (
            previous_provider_key is not None
            and canonical_provider_key <= previous_provider_key
        ):
            diagnostics.append(
                _diagnostic(
                    "factory.binding-plan.not-normalized",
                    f"/providers/{provider_index}",
                    "providers are not in canonical order",
                )
            )
        previous_provider_key = canonical_provider_key

        ordered_factory_ids = sorted(provider["factoryIds"], key=_utf8_key)
        if provider["factoryIds"] != ordered_factory_ids:
            diagnostics.append(
                _diagnostic(
                    "factory.binding-plan.not-normalized",
                    f"/providers/{provider_index}/factoryIds",
                    "provider factory IDs are not in canonical order",
                )
            )
        for factory_index, factory_id in enumerate(provider["factoryIds"]):
            factory_key = (
                provider["packageId"],
                provider["packageVersion"],
                provider["moduleId"],
                factory_id,
            )
            if factory_key in factory_keys:
                diagnostics.append(
                    _diagnostic(
                        "factory.binding-plan.duplicate-factory",
                        f"/providers/{provider_index}/factoryIds/{factory_index}",
                        "selected factory is bound by more than one provider",
                    )
                )
            factory_keys.add(factory_key)

    expected_integrity = contracts.compute_bytes_integrity(
        json.dumps(
            {key: value for key, value in data.items() if key != "integrity"},
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    )
    if data["integrity"] != expected_integrity:
        diagnostics.append(
            _diagnostic(
                "factory.binding-plan.integrity-mismatch",
                "/integrity",
                "binding plan integrity does not match canonical fields",
            )
        )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _verified_static_bindings(
    candidate: PackageCandidate,
    validators: contracts.ContractValidators,
) -> tuple[dict[str, Any] | None, list[contracts.Diagnostic]]:
    path = (
        f"candidate/{candidate.identity}/"
        f"{contracts.PACKAGE_STATIC_FACTORY_BINDINGS_NAME}"
    )
    values = (
        candidate.static_factory_bindings,
        candidate.static_factory_bindings_integrity,
        candidate.static_factory_bindings_bytes,
    )
    if all(value is None for value in values):
        return None, [
            contracts.Diagnostic(
                code="factory.binding.missing",
                manifest_path=path,
                pointer="",
                message=(
                    f"selected native package '{candidate.identity}' has no static "
                    "factory bindings"
                ),
            )
        ]
    if not (
        isinstance(candidate.static_factory_bindings, dict)
        and isinstance(candidate.static_factory_bindings_integrity, dict)
        and isinstance(candidate.static_factory_bindings_bytes, bytes)
    ):
        return None, [
            contracts.Diagnostic(
                code="factory.binding.snapshot-incomplete",
                manifest_path=path,
                pointer="",
                message="selected static factory binding snapshot is incomplete",
            )
        ]

    diagnostics: list[contracts.Diagnostic] = []
    if contracts.compute_bytes_integrity(
        candidate.static_factory_bindings_bytes
    ) != candidate.static_factory_bindings_integrity:
        diagnostics.append(
            contracts.Diagnostic(
                code="factory.binding.snapshot-integrity",
                manifest_path=path,
                pointer="",
                message="static factory binding bytes changed after verification",
            )
        )
    try:
        parsed = json.loads(candidate.static_factory_bindings_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        parsed = None
    if parsed != candidate.static_factory_bindings:
        diagnostics.append(
            contracts.Diagnostic(
                code="factory.binding.snapshot-data",
                manifest_path=path,
                pointer="",
                message="static factory binding data differs from exact bytes",
            )
        )
    diagnostics.extend(
        contracts.validate_package_static_factory_bindings(
            candidate.static_factory_bindings,
            candidate.manifest,
            candidate.build_descriptor,
            candidate.factory_declaration,
            validators,
            bindings_path=path,
            manifest_path=(
                f"candidate/{candidate.identity}/{contracts.PACKAGE_MANIFEST_NAME}"
            ),
            build_descriptor_path=(
                f"candidate/{candidate.identity}/{contracts.PACKAGE_SOURCE_BUILD_NAME}"
            ),
            factory_declaration_path=(
                f"candidate/{candidate.identity}/{contracts.PACKAGE_FACTORIES_NAME}"
            ),
        )
    )
    return (
        candidate.static_factory_bindings if not diagnostics else None,
        diagnostics,
    )


def plan_static_factory_provider_bindings(
    session: EffectiveSessionPlan,
    build_plan: source_build_plan.SourceBuildPlan,
    blueprint: activation.HostActivationBlueprint,
    validators: contracts.ContractValidators,
) -> StaticFactoryProviderBindingResult:
    """Cross-bind selected factories and build targets without filesystem access."""

    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(session, EffectiveSessionPlan):
        diagnostics.append(
            _diagnostic(
                "factory.binding.input.ready-session-required",
                "/inputs/effectiveSessionIntegrity",
                "provider binding requires a Ready Effective Session",
            )
        )
    else:
        diagnostics.extend(
            contracts.Diagnostic(
                code=value.code,
                manifest_path=value.manifest_path,
                pointer=value.pointer,
                message=value.message,
            )
            for value in validate_ready_effective_session(session, validators)
        )
    if not isinstance(build_plan, source_build_plan.SourceBuildPlan):
        diagnostics.append(
            _diagnostic(
                "factory.binding.input.source-build-plan-required",
                "/inputs/sourceBuildPlanIntegrity",
                "provider binding requires a Source Build Plan",
            )
        )
    else:
        diagnostics.extend(
            source_build_plan.validate_source_build_plan_data(build_plan, validators)
        )
    if not isinstance(blueprint, activation.HostActivationBlueprint):
        diagnostics.append(
            _diagnostic(
                "factory.binding.input.activation-blueprint-required",
                "/inputs/hostActivationBlueprintIntegrity",
                "provider binding requires a Host Activation Blueprint",
            )
        )
    else:
        diagnostics.extend(
            activation.validate_host_activation_blueprint_data(
                blueprint,
                validators,
            )
        )
    if diagnostics:
        return _failure(diagnostics)

    assert isinstance(session, EffectiveSessionPlan)
    assert isinstance(build_plan, source_build_plan.SourceBuildPlan)
    assert isinstance(blueprint, activation.HostActivationBlueprint)
    if blueprint.inputs.effective_session_integrity != activation.IntegrityRecord(
        session.session_fingerprint.algorithm,
        session.session_fingerprint.digest,
    ):
        diagnostics.append(
            _diagnostic(
                "factory.binding.input.session-mismatch",
                "/inputs/effectiveSessionIntegrity",
                "Activation Blueprint does not belong to the Effective Session",
            )
        )
    if (
        build_plan.inputs.host_composition_integrity.algorithm
        != blueprint.inputs.host_composition_integrity.algorithm
        or build_plan.inputs.host_composition_integrity.digest
        != blueprint.inputs.host_composition_integrity.digest
    ):
        diagnostics.append(
            _diagnostic(
                "factory.binding.input.host-composition-mismatch",
                "/inputs",
                "Source Build Plan and Activation Blueprint use different Host compositions",
            )
        )
    if (
        build_plan.host_kind != blueprint.host_kind
        or build_plan.target_platform != blueprint.target_platform
        or session.host_kind != blueprint.host_kind
        or session.target_platform != blueprint.target_platform
    ):
        diagnostics.append(
            _diagnostic(
                "factory.binding.input.host-mismatch",
                "/host",
                "Session, Source Build Plan, and Activation Blueprint Host facts differ",
            )
        )
    if blueprint.engine_generation_id != session.verified_graph.engine_generation_id:
        diagnostics.append(
            _diagnostic(
                "factory.binding.input.engine-generation-mismatch",
                "/host/engineGenerationId",
                "Activation Blueprint Engine generation differs from the Session",
            )
        )
    if diagnostics:
        return _failure(diagnostics)

    selected_references = tuple(
        factory.reference
        for scope in blueprint.scope_templates
        for factory in scope.factories
    )
    selected_keys = {
        (
            reference.package_id,
            reference.package_version,
            reference.module_id,
            reference.factory_id,
        )
        for reference in selected_references
    }
    candidates_by_id: dict[str, list[PackageCandidate]] = {}
    for candidate in session.verified_graph.selected_candidates:
        candidates_by_id.setdefault(candidate.identity, []).append(candidate)
    build_modules = {
        (package.package_id, package.package_version, module.module_id): module
        for package in build_plan.packages
        for module in package.modules
    }
    selected_root_targets = {
        (target.name, target.target_type) for target in build_plan.build_roots
    }
    closure_targets = {
        (target.name, target.target_type) for target in build_plan.target_closure
    }

    providers: list[StaticFactoryProvider] = []
    mapped_factories: set[tuple[str, str, str, str]] = set()
    declaration_evidence: list[dict[str, Any]] = []
    selected_packages = sorted(
        {(value[0], value[1]) for value in selected_keys},
        key=lambda value: (_utf8_key(value[0]), _utf8_key(value[1])),
    )
    for package_id, package_version in selected_packages:
        matches = [
            candidate
            for candidate in candidates_by_id.get(package_id, [])
            if candidate.version == package_version
        ]
        if len(matches) != 1:
            diagnostics.append(
                _diagnostic(
                    "factory.binding.input.candidate-mismatch",
                    "/providers",
                    f"selected package '{package_id}' must have one exact candidate",
                )
            )
            continue
        candidate = matches[0]
        bindings, binding_diagnostics = _verified_static_bindings(
            candidate,
            validators,
        )
        diagnostics.extend(binding_diagnostics)
        if bindings is None:
            continue
        assert candidate.static_factory_bindings_integrity is not None
        declaration_evidence.append(
            {
                "packageId": package_id,
                "packageVersion": package_version,
                "integrity": candidate.static_factory_bindings_integrity,
                "bindings": contracts.normalize_package_static_factory_bindings(
                    bindings
                ),
            }
        )

        for binding_module in bindings["modules"]:
            module_id = binding_module["moduleId"]
            module_selected_keys = {
                value
                for value in selected_keys
                if value[:3] == (package_id, package_version, module_id)
            }
            if not module_selected_keys:
                continue
            binding = binding_module["binding"]
            if binding["kind"] != "provider-set":
                diagnostics.append(
                    _diagnostic(
                        "factory.binding.provider-set-missing",
                        "/providers",
                        f"selected module '{package_id}:{module_id}' has no provider set",
                    )
                )
                continue
            build_module = build_modules.get(
                (package_id, package_version, module_id)
            )
            if build_module is None:
                diagnostics.append(
                    _diagnostic(
                        "factory.binding.module-unselected",
                        "/providers",
                        (
                            f"selected factory module '{package_id}:{module_id}' is "
                            "absent from the Source Build Plan"
                        ),
                    )
                )
                continue
            selected_module_targets = {
                (target.name, target.target_type) for target in build_module.targets
            }

            for provider in binding["providers"]:
                provider_factory_keys = {
                    (package_id, package_version, module_id, factory_id)
                    for factory_id in provider["factoryIds"]
                }
                selected_provider_factories = (
                    provider_factory_keys & module_selected_keys
                )
                if not selected_provider_factories:
                    continue
                if selected_provider_factories != provider_factory_keys:
                    diagnostics.append(
                        _diagnostic(
                            "factory.binding.provider-partial-selection",
                            "/providers",
                            (
                                f"provider '{provider['entryPoint']['function']}' "
                                "mixes selected and unselected factories"
                            ),
                        )
                    )
                    continue
                target_key = (
                    provider["target"]["name"],
                    provider["target"]["type"],
                )
                if (
                    target_key not in selected_module_targets
                    or target_key not in selected_root_targets
                    or target_key not in closure_targets
                ):
                    diagnostics.append(
                        _diagnostic(
                            "factory.binding.target-unselected",
                            "/providers",
                            (
                                f"provider target '{target_key[0]}' for "
                                f"'{package_id}:{module_id}' is not selected by the "
                                "Source Build Plan"
                            ),
                        )
                    )
                    continue
                mapped_factories.update(selected_provider_factories)
                providers.append(
                    StaticFactoryProvider(
                        package_id=package_id,
                        package_version=package_version,
                        module_id=module_id,
                        target=ProviderTarget(*target_key),
                        entry_point=ProviderEntryPoint(
                            provider["entryPoint"]["header"],
                            provider["entryPoint"]["function"],
                        ),
                        factory_ids=tuple(
                            sorted(provider["factoryIds"], key=_utf8_key)
                        ),
                    )
                )

    for missing in sorted(
        selected_keys - mapped_factories,
        key=lambda value: tuple(_utf8_key(part) for part in value),
    ):
        diagnostics.append(
            _diagnostic(
                "factory.binding.factory-unbound",
                "/providers",
                (
                    "selected factory has no verified provider: "
                    f"{missing[0]}@{missing[1]}:{missing[2]}:{missing[3]}"
                ),
            )
        )
    if diagnostics:
        return _failure(diagnostics)

    inputs = StaticFactoryProviderBindingInputs(
        effective_session_integrity=IntegrityRecord(
            session.session_fingerprint.algorithm,
            session.session_fingerprint.digest,
        ),
        source_build_plan_integrity=IntegrityRecord(
            build_plan.integrity.algorithm,
            build_plan.integrity.digest,
        ),
        host_activation_blueprint_integrity=IntegrityRecord(
            blueprint.integrity.algorithm,
            blueprint.integrity.digest,
        ),
        binding_declaration_set_integrity=_canonical_integrity(
            declaration_evidence
        ),
    )
    plan = StaticFactoryProviderBindingPlan(
        inputs=inputs,
        engine_generation_id=blueprint.engine_generation_id,
        host_kind=blueprint.host_kind,
        target_platform=blueprint.target_platform,
        providers=tuple(sorted(providers, key=_provider_sort_key)),
        integrity=IntegrityRecord("sha256", "0" * 64),
    )
    plan = replace(
        plan,
        integrity=_integrity_record(
            compute_static_factory_provider_binding_plan_integrity(plan)
        ),
    )
    output_diagnostics = validate_static_factory_provider_binding_plan_data(
        plan,
        validators,
    )
    if output_diagnostics:
        return _failure(output_diagnostics)
    return StaticFactoryProviderBindingResult(plan=plan, diagnostics=())
