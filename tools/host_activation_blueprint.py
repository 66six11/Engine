"""Deterministic pre-build Host Activation Blueprint planning."""

from __future__ import annotations

import json
from dataclasses import dataclass, replace
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_package_composition as composition
from tools.effective_session import EffectiveSessionPlan, validate_ready_effective_session
from tools.package_candidates import PackageCandidate


HOST_ACTIVATION_BLUEPRINT_NAME = "asharia.host-activation-blueprint.json"
HOST_ACTIVATION_BLUEPRINT_SCHEMA = "com.asharia.host-activation-blueprint"
HOST_ACTIVATION_BLUEPRINT_SCHEMA_VERSION = 1
LIFECYCLE_MODEL = "create-activate-quiesce-deactivate-destroy-v1"

SCOPE_ORDER = (
    "process",
    "project",
    "editor",
    "editor-document",
    "preview",
    "game-session",
    "world",
    "local-user",
    "tool-job",
)
SCOPE_PARENT: dict[str, str | None] = {
    "process": None,
    "project": "process",
    "editor": "project",
    "tool-job": "process",
    "game-session": "project",
    "world": "game-session",
    "local-user": "game-session",
    "editor-document": "editor",
    "preview": "editor",
}
HOST_SCOPE_POLICIES: dict[str, tuple[str, ...]] = {
    "minimal": ("process",),
    "editor": SCOPE_ORDER,
    "runtime": ("process", "project", "game-session", "world", "local-user"),
    "dedicated-server": ("process", "project", "game-session", "world"),
    "asset-worker": ("process", "tool-job"),
}


@dataclass(frozen=True, order=True)
class IntegrityRecord:
    """Immutable SHA-256 evidence."""

    algorithm: str
    digest: str


@dataclass(frozen=True, order=True)
class ExactFactoryReference:
    """One exact logical factory selected for a Host build."""

    package_id: str
    package_version: str
    module_id: str
    factory_id: str


@dataclass(frozen=True, order=True)
class FactoryContribution:
    """One Host-selected contribution owned by a selected factory."""

    contribution_id: str
    contribution_kind: str


@dataclass(frozen=True)
class FactoryActivation:
    """One factory template with exact dependency and contribution bindings."""

    reference: ExactFactoryReference
    requirements: tuple[ExactFactoryReference, ...]
    contributions: tuple[FactoryContribution, ...]


@dataclass(frozen=True)
class ScopeActivationTemplate:
    """Factory order applied whenever one runtime scope instance is created."""

    scope: str
    parent_scope: str | None
    factories: tuple[FactoryActivation, ...]


@dataclass(frozen=True)
class HostActivationBlueprintInputs:
    """Canonical evidence for each independent blueprint authority."""

    effective_session_integrity: IntegrityRecord
    host_composition_integrity: IntegrityRecord
    factory_declaration_set_integrity: IntegrityRecord


@dataclass(frozen=True)
class HostActivationBlueprint:
    """Canonical pre-build factory plan; deliberately not an executable receipt."""

    inputs: HostActivationBlueprintInputs
    engine_generation_id: str
    host_kind: str
    target_platform: str
    lifecycle_model: str
    scope_templates: tuple[ScopeActivationTemplate, ...]
    integrity: IntegrityRecord


@dataclass(frozen=True)
class HostActivationBlueprintResult:
    """Atomic result: either one complete blueprint or stable diagnostics."""

    blueprint: HostActivationBlueprint | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.blueprint is not None and not self.diagnostics


@dataclass(frozen=True)
class _FactoryDraft:
    reference: ExactFactoryReference
    scope: str
    declared_requirements: tuple[tuple[str, str], ...]
    claimed_contributions: tuple[str, ...]


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8")


def _reference_key(reference: ExactFactoryReference) -> tuple[bytes, bytes, bytes]:
    return (
        _utf8_key(reference.package_id),
        _utf8_key(reference.package_version),
        _utf8_key(reference.factory_id),
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
        manifest_path=HOST_ACTIVATION_BLUEPRINT_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostActivationBlueprintResult:
    return HostActivationBlueprintResult(
        blueprint=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _integrity_record(value: dict[str, str]) -> IntegrityRecord:
    return IntegrityRecord(value["algorithm"], value["digest"])


def _integrity_data(value: IntegrityRecord) -> dict[str, str]:
    return {"algorithm": value.algorithm, "digest": value.digest}


def _reference_data(reference: ExactFactoryReference) -> dict[str, str]:
    return {
        "packageId": reference.package_id,
        "packageVersion": reference.package_version,
        "moduleId": reference.module_id,
        "factoryId": reference.factory_id,
    }


def _reference_from_data(value: dict[str, str]) -> ExactFactoryReference:
    return ExactFactoryReference(
        package_id=value["packageId"],
        package_version=value["packageVersion"],
        module_id=value["moduleId"],
        factory_id=value["factoryId"],
    )


def _blueprint_payload_data(blueprint: HostActivationBlueprint) -> dict[str, Any]:
    return {
        "schema": HOST_ACTIVATION_BLUEPRINT_SCHEMA,
        "schemaVersion": HOST_ACTIVATION_BLUEPRINT_SCHEMA_VERSION,
        "inputs": {
            "effectiveSessionIntegrity": _integrity_data(
                blueprint.inputs.effective_session_integrity
            ),
            "hostCompositionIntegrity": _integrity_data(
                blueprint.inputs.host_composition_integrity
            ),
            "factoryDeclarationSetIntegrity": _integrity_data(
                blueprint.inputs.factory_declaration_set_integrity
            ),
        },
        "host": {
            "engineGenerationId": blueprint.engine_generation_id,
            "hostKind": blueprint.host_kind,
            "targetPlatform": blueprint.target_platform,
        },
        "lifecycleModel": blueprint.lifecycle_model,
        "scopeTemplates": [
            {
                "scope": template.scope,
                "parentScope": template.parent_scope,
                "factories": [
                    {
                        "reference": _reference_data(factory.reference),
                        "requires": [
                            _reference_data(reference)
                            for reference in factory.requirements
                        ],
                        "contributions": [
                            {
                                "id": contribution.contribution_id,
                                "kind": contribution.contribution_kind,
                            }
                            for contribution in factory.contributions
                        ],
                    }
                    for factory in template.factories
                ],
            }
            for template in blueprint.scope_templates
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


def host_activation_blueprint_to_data(
    blueprint: HostActivationBlueprint,
) -> dict[str, Any]:
    """Return the fixed-field-order JSON-compatible blueprint representation."""

    return {
        **_blueprint_payload_data(blueprint),
        "integrity": _integrity_data(blueprint.integrity),
    }


def render_host_activation_blueprint(blueprint: HostActivationBlueprint) -> str:
    """Render canonical JSON text with LF and a final newline."""

    return json.dumps(
        host_activation_blueprint_to_data(blueprint),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def compute_host_activation_blueprint_integrity(
    blueprint: HostActivationBlueprint,
) -> dict[str, str]:
    """Hash all canonical blueprint fields except self-integrity."""

    return _data_integrity(_blueprint_payload_data(blueprint))


def _scope_ancestors(scope: str) -> tuple[str, ...]:
    ancestors: list[str] = []
    parent = SCOPE_PARENT[scope]
    while parent is not None:
        ancestors.append(parent)
        parent = SCOPE_PARENT[parent]
    return tuple(ancestors)


def validate_host_activation_blueprint_data(
    blueprint: HostActivationBlueprint | Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Validate the closed blueprint shape, semantics, ordering, and integrity."""

    data = (
        host_activation_blueprint_to_data(blueprint)
        if isinstance(blueprint, HostActivationBlueprint)
        else blueprint
    )
    diagnostics = contracts.validate_manifest_data(
        data,
        HOST_ACTIVATION_BLUEPRINT_NAME,
        validators,
    )
    if diagnostics or not isinstance(data, dict):
        return diagnostics

    host_kind = data["host"]["hostKind"]
    expected_scopes = HOST_SCOPE_POLICIES[host_kind]
    scope_templates = data["scopeTemplates"]
    actual_scopes = tuple(template["scope"] for template in scope_templates)
    if actual_scopes != expected_scopes:
        diagnostics.append(
            _diagnostic(
                "activation.blueprint.scope-policy",
                "/scopeTemplates",
                f"scope templates do not match fixed host policy for '{host_kind}'",
            )
        )

    locations: dict[ExactFactoryReference, tuple[str, int]] = {}
    contribution_owners: dict[tuple[str, str], ExactFactoryReference] = {}
    for template_index, template in enumerate(scope_templates):
        scope = template["scope"]
        if template["parentScope"] != SCOPE_PARENT[scope]:
            diagnostics.append(
                _diagnostic(
                    "activation.blueprint.scope-parent",
                    f"/scopeTemplates/{template_index}/parentScope",
                    f"scope '{scope}' does not use its fixed parent",
                )
            )
        for factory_index, factory in enumerate(template["factories"]):
            reference = _reference_from_data(factory["reference"])
            previous = locations.get(reference)
            if previous is not None:
                diagnostics.append(
                    _diagnostic(
                        "activation.blueprint.factory-duplicate",
                        f"/scopeTemplates/{template_index}/factories/{factory_index}/reference",
                        f"factory '{reference.package_id}:{reference.factory_id}' appears more than once",
                    )
                )
            else:
                locations[reference] = (scope, factory_index)
            for contribution_index, contribution in enumerate(
                factory["contributions"]
            ):
                key = (reference.package_id, contribution["id"])
                owner = contribution_owners.get(key)
                if owner is not None:
                    diagnostics.append(
                        _diagnostic(
                            "activation.blueprint.contribution-duplicate",
                            (
                                f"/scopeTemplates/{template_index}/factories/"
                                f"{factory_index}/contributions/{contribution_index}"
                            ),
                            f"contribution '{contribution['id']}' has multiple factory owners",
                        )
                    )
                else:
                    contribution_owners[key] = reference

    for template_index, template in enumerate(scope_templates):
        scope = template["scope"]
        allowed_scopes = {scope, *_scope_ancestors(scope)}
        for factory_index, factory in enumerate(template["factories"]):
            source = _reference_from_data(factory["reference"])
            for requirement_index, value in enumerate(factory["requires"]):
                target = _reference_from_data(value)
                target_location = locations.get(target)
                pointer = (
                    f"/scopeTemplates/{template_index}/factories/{factory_index}/"
                    f"requires/{requirement_index}"
                )
                if target_location is None:
                    diagnostics.append(
                        _diagnostic(
                            "activation.blueprint.factory-missing",
                            pointer,
                            f"required factory '{target.package_id}:{target.factory_id}' is absent",
                        )
                    )
                    continue
                target_scope, target_index = target_location
                if target_scope not in allowed_scopes:
                    diagnostics.append(
                        _diagnostic(
                            "activation.blueprint.scope-direction",
                            pointer,
                            (
                                f"'{scope}' factory '{source.package_id}:{source.factory_id}' "
                                f"cannot require '{target_scope}' factory "
                                f"'{target.package_id}:{target.factory_id}'"
                            ),
                        )
                    )
                elif target_scope == scope and target_index >= factory_index:
                    diagnostics.append(
                        _diagnostic(
                            "activation.blueprint.factory-order",
                            pointer,
                            (
                                f"same-scope requirement '{target.package_id}:"
                                f"{target.factory_id}' must precede its dependent"
                            ),
                        )
                    )

    payload = {key: value for key, value in data.items() if key != "integrity"}
    if data["integrity"] != _data_integrity(payload):
        diagnostics.append(
            _diagnostic(
                "activation.blueprint.integrity-mismatch",
                "/integrity",
                "Host Activation Blueprint integrity does not match canonical fields",
            )
        )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _verify_inputs(
    session: Any,
    host_plan: Any,
    validators: contracts.ContractValidators,
) -> tuple[
    EffectiveSessionPlan | None,
    composition.HostCompositionPlan | None,
    tuple[PackageCandidate, ...],
    list[contracts.Diagnostic],
]:
    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(session, EffectiveSessionPlan):
        diagnostics.append(
            _diagnostic(
                "activation.input.ready-session-required",
                "",
                "planner requires one Ready EffectiveSessionPlan",
            )
        )
    else:
        diagnostics.extend(
            _diagnostic(value.code, value.pointer, value.message)
            for value in validate_ready_effective_session(session, validators)
        )

    if not isinstance(host_plan, composition.HostCompositionPlan):
        diagnostics.append(
            _diagnostic(
                "activation.input.host-composition-required",
                "",
                "planner requires one matching HostCompositionPlan",
            )
        )
    else:
        diagnostics.extend(
            composition.validate_host_composition_plan_data(
                composition.host_composition_plan_to_data(host_plan),
                validators,
            )
        )

    if diagnostics:
        return None, None, (), diagnostics
    assert isinstance(session, EffectiveSessionPlan)
    assert isinstance(host_plan, composition.HostCompositionPlan)

    expected = composition.plan_host_package_composition(session, validators)
    if not expected.succeeded:
        return None, None, (), [
            _diagnostic(value.code, value.pointer, value.message)
            for value in expected.diagnostics
        ]
    assert expected.plan is not None
    if composition.render_host_composition_plan(
        expected.plan
    ) != composition.render_host_composition_plan(host_plan):
        diagnostics.append(
            _diagnostic(
                "activation.input.host-composition-stale",
                "/inputs/hostCompositionIntegrity",
                "Host Composition Plan does not match the Ready Effective Session",
            )
        )
    if diagnostics:
        return None, None, (), diagnostics
    return (
        session,
        expected.plan,
        session.verified_graph.selected_candidates,
        [],
    )


def _verified_factory_declaration(
    candidate: PackageCandidate,
    validators: contracts.ContractValidators,
) -> tuple[dict[str, Any] | None, dict[str, str] | None, list[contracts.Diagnostic]]:
    path = f"candidate/{candidate.identity}/{contracts.PACKAGE_FACTORIES_NAME}"
    values = (
        candidate.factory_declaration,
        candidate.factory_declaration_integrity,
        candidate.factory_declaration_bytes,
    )
    present_count = sum(value is not None for value in values)
    if present_count == 0:
        return None, None, [
            _diagnostic(
                "activation.declaration.missing",
                "/packages",
                f"selected package '{candidate.identity}' has no factory declaration",
            )
        ]
    if present_count != 3:
        return None, None, [
            _diagnostic(
                "activation.declaration.incomplete",
                "/packages",
                f"selected package '{candidate.identity}' has an incomplete factory snapshot",
            )
        ]

    declaration = candidate.factory_declaration
    integrity = candidate.factory_declaration_integrity
    exact_bytes = candidate.factory_declaration_bytes
    if (
        not isinstance(declaration, dict)
        or not isinstance(integrity, dict)
        or not isinstance(exact_bytes, bytes)
    ):
        return None, None, [
            _diagnostic(
                "activation.declaration.invalid",
                "/packages",
                f"selected package '{candidate.identity}' has invalid factory evidence types",
            )
        ]

    diagnostics: list[contracts.Diagnostic] = []
    if contracts.compute_bytes_integrity(exact_bytes) != integrity:
        diagnostics.append(
            _diagnostic(
                "activation.declaration.integrity-mismatch",
                "/inputs/factoryDeclarationSetIntegrity",
                f"factory declaration bytes changed for '{candidate.identity}'",
            )
        )
    try:
        parsed = json.loads(exact_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        parsed = None
    if parsed != declaration:
        diagnostics.append(
            _diagnostic(
                "activation.declaration.snapshot-mismatch",
                "/inputs/factoryDeclarationSetIntegrity",
                f"factory declaration data does not match exact bytes for '{candidate.identity}'",
            )
        )
    diagnostics.extend(
        contracts.validate_package_factory_declaration_binding(
            declaration,
            candidate.manifest,
            validators,
            declaration_path=path,
            manifest_path=f"candidate/{candidate.identity}/{contracts.PACKAGE_MANIFEST_NAME}",
        )
    )
    if diagnostics:
        return None, None, diagnostics
    return declaration, integrity, []


def _dependency_first_order(
    drafts: dict[tuple[str, str], _FactoryDraft],
    requirements: dict[tuple[str, str], tuple[ExactFactoryReference, ...]],
) -> tuple[tuple[tuple[str, str], ...] | None, list[contracts.Diagnostic]]:
    dependencies = {
        key: {(value.package_id, value.factory_id) for value in values}
        for key, values in requirements.items()
    }
    dependents: dict[tuple[str, str], set[tuple[str, str]]] = {
        key: set() for key in drafts
    }
    for key, values in dependencies.items():
        for dependency in values:
            dependents[dependency].add(key)

    def key(value: tuple[str, str]) -> tuple[bytes, bytes, bytes]:
        return _reference_key(drafts[value].reference)

    ready = [identity for identity, values in dependencies.items() if not values]
    ordered: list[tuple[str, str]] = []
    while ready:
        ready.sort(key=key)
        identity = ready.pop(0)
        ordered.append(identity)
        for dependent in sorted(dependents[identity], key=key):
            dependencies[dependent].discard(identity)
            if not dependencies[dependent] and dependent not in ready:
                ready.append(dependent)

    if len(ordered) != len(drafts):
        remaining = sorted(set(drafts) - set(ordered), key=key)
        labels = ", ".join(f"{package}:{factory}" for package, factory in remaining)
        return None, [
            _diagnostic(
                "activation.factory.cycle",
                "/scopeTemplates",
                f"factory dependency cycle includes: {labels}",
            )
        ]
    return tuple(ordered), []


def plan_host_activation_blueprint(
    session: Any,
    host_plan: Any,
    validators: contracts.ContractValidators,
) -> HostActivationBlueprintResult:
    """Derive one canonical scope-template blueprint from verified logical inputs."""

    verified_session, verified_host_plan, candidates, diagnostics = _verify_inputs(
        session,
        host_plan,
        validators,
    )
    if diagnostics:
        return _failure(diagnostics)
    assert verified_session is not None
    assert verified_host_plan is not None

    candidates_by_id: dict[str, list[PackageCandidate]] = {}
    for candidate in candidates:
        candidates_by_id.setdefault(candidate.identity, []).append(candidate)

    allowed_scopes = set(HOST_SCOPE_POLICIES[verified_host_plan.host_kind])
    drafts: dict[tuple[str, str], _FactoryDraft] = {}
    declaration_evidence: list[dict[str, Any]] = []
    for package in verified_host_plan.packages:
        package_id = package.package.package_id
        matches = candidates_by_id.get(package_id, [])
        if len(matches) != 1:
            diagnostics.append(
                _diagnostic(
                    "activation.input.candidate-mismatch",
                    "/packages",
                    f"Host package '{package_id}' must have exactly one verified candidate",
                )
            )
            continue
        candidate = matches[0]
        if (
            candidate.version != package.package.package_version
            or candidate.package_kind != "installable-capability"
        ):
            diagnostics.append(
                _diagnostic(
                    "activation.input.candidate-mismatch",
                    "/packages",
                    f"Host package '{package_id}' does not match its exact candidate",
                )
            )
            continue

        declaration, declaration_integrity, declaration_diagnostics = (
            _verified_factory_declaration(candidate, validators)
        )
        diagnostics.extend(declaration_diagnostics)
        if declaration is None or declaration_integrity is None:
            continue
        declaration_evidence.append(
            {
                "packageId": package_id,
                "packageVersion": candidate.version,
                "integrity": declaration_integrity,
            }
        )

        selected_modules = {module.module_id for module in package.modules}
        normalized = contracts.normalize_package_factory_declaration(declaration)
        for module in normalized["modules"]:
            module_id = module["moduleId"]
            if module_id not in selected_modules:
                continue
            activation = module["activation"]
            if activation["kind"] == "no-factories":
                continue
            for factory in activation["factories"]:
                identity = (package_id, factory["id"])
                reference = ExactFactoryReference(
                    package_id=package_id,
                    package_version=candidate.version,
                    module_id=module_id,
                    factory_id=factory["id"],
                )
                if identity in drafts:
                    diagnostics.append(
                        _diagnostic(
                            "activation.factory.duplicate",
                            "/packages",
                            f"selected factory '{package_id}:{factory['id']}' is duplicated",
                        )
                    )
                    continue
                if factory["scope"] not in allowed_scopes:
                    diagnostics.append(
                        _diagnostic(
                            "activation.factory.scope-policy",
                            "/scopeTemplates",
                            (
                                f"host '{verified_host_plan.host_kind}' does not support "
                                f"scope '{factory['scope']}' for factory "
                                f"'{package_id}:{factory['id']}'"
                            ),
                        )
                    )
                drafts[identity] = _FactoryDraft(
                    reference=reference,
                    scope=factory["scope"],
                    declared_requirements=tuple(
                        (value["packageId"], value["factoryId"])
                        for value in factory["requires"]
                    ),
                    claimed_contributions=tuple(factory["contributions"]),
                )

    if diagnostics:
        return _failure(diagnostics)

    requirements: dict[tuple[str, str], tuple[ExactFactoryReference, ...]] = {}
    for identity, draft in drafts.items():
        resolved: list[ExactFactoryReference] = []
        allowed_required_scopes = {draft.scope, *_scope_ancestors(draft.scope)}
        for required_identity in draft.declared_requirements:
            target = drafts.get(required_identity)
            if target is None:
                diagnostics.append(
                    _diagnostic(
                        "activation.factory.missing",
                        "/scopeTemplates",
                        (
                            f"factory '{draft.reference.package_id}:"
                            f"{draft.reference.factory_id}' requires unselected factory "
                            f"'{required_identity[0]}:{required_identity[1]}'"
                        ),
                    )
                )
                continue
            if target.scope not in allowed_required_scopes:
                diagnostics.append(
                    _diagnostic(
                        "activation.factory.scope-direction",
                        "/scopeTemplates",
                        (
                            f"'{draft.scope}' factory '{draft.reference.package_id}:"
                            f"{draft.reference.factory_id}' cannot require "
                            f"'{target.scope}' factory '{target.reference.package_id}:"
                            f"{target.reference.factory_id}'"
                        ),
                    )
                )
            resolved.append(target.reference)
        requirements[identity] = tuple(sorted(resolved, key=_reference_key))

    if diagnostics:
        return _failure(diagnostics)
    ordered, order_diagnostics = _dependency_first_order(drafts, requirements)
    if order_diagnostics:
        return _failure(order_diagnostics)
    assert ordered is not None

    selected_contributions: dict[
        tuple[str, str], composition.HostContributionComposition
    ] = {}
    for contribution in verified_host_plan.contributions:
        key = (contribution.package_id, contribution.contribution_id)
        if key in selected_contributions:
            diagnostics.append(
                _diagnostic(
                    "activation.contribution.duplicate",
                    "/scopeTemplates",
                    f"selected contribution '{contribution.contribution_id}' is duplicated",
                )
            )
        else:
            selected_contributions[key] = contribution

    factory_contributions: dict[
        tuple[str, str], tuple[FactoryContribution, ...]
    ] = {}
    claimed_selected: dict[tuple[str, str], tuple[str, str]] = {}
    for identity, draft in drafts.items():
        values: list[FactoryContribution] = []
        for contribution_id in draft.claimed_contributions:
            contribution_key = (draft.reference.package_id, contribution_id)
            selected = selected_contributions.get(contribution_key)
            if selected is None:
                continue
            if (
                selected.package_version != draft.reference.package_version
                or selected.owner_module_id != draft.reference.module_id
            ):
                diagnostics.append(
                    _diagnostic(
                        "activation.contribution.owner-mismatch",
                        "/scopeTemplates",
                        f"selected contribution '{contribution_id}' does not match its factory owner",
                    )
                )
                continue
            previous = claimed_selected.get(contribution_key)
            if previous is not None:
                diagnostics.append(
                    _diagnostic(
                        "activation.contribution.duplicate-owner",
                        "/scopeTemplates",
                        f"selected contribution '{contribution_id}' has multiple factory owners",
                    )
                )
                continue
            claimed_selected[contribution_key] = identity
            values.append(
                FactoryContribution(
                    contribution_id=contribution_id,
                    contribution_kind=selected.contribution_kind,
                )
            )
        factory_contributions[identity] = tuple(
            sorted(values, key=lambda value: _utf8_key(value.contribution_id))
        )

    for contribution_key, contribution in selected_contributions.items():
        if contribution_key not in claimed_selected:
            diagnostics.append(
                _diagnostic(
                    "activation.contribution.owner-missing",
                    "/scopeTemplates",
                    (
                        f"selected contribution '{contribution.contribution_id}' has no "
                        "selected factory owner"
                    ),
                )
            )
    if diagnostics:
        return _failure(diagnostics)

    factories_by_scope: dict[str, list[FactoryActivation]] = {
        scope: [] for scope in HOST_SCOPE_POLICIES[verified_host_plan.host_kind]
    }
    for identity in ordered:
        draft = drafts[identity]
        factories_by_scope[draft.scope].append(
            FactoryActivation(
                reference=draft.reference,
                requirements=requirements[identity],
                contributions=factory_contributions[identity],
            )
        )

    inputs = HostActivationBlueprintInputs(
        effective_session_integrity=IntegrityRecord(
            verified_session.session_fingerprint.algorithm,
            verified_session.session_fingerprint.digest,
        ),
        host_composition_integrity=_integrity_record(
            contracts.compute_bytes_integrity(
                composition.render_host_composition_plan(
                    verified_host_plan
                ).encode("utf-8")
            )
        ),
        factory_declaration_set_integrity=_integrity_record(
            _data_integrity(
                sorted(
                    declaration_evidence,
                    key=lambda value: (
                        _utf8_key(value["packageId"]),
                        _utf8_key(value["packageVersion"]),
                    ),
                )
            )
        ),
    )
    blueprint = HostActivationBlueprint(
        inputs=inputs,
        engine_generation_id=verified_session.verified_graph.engine_generation_id,
        host_kind=verified_host_plan.host_kind,
        target_platform=verified_host_plan.target_platform,
        lifecycle_model=LIFECYCLE_MODEL,
        scope_templates=tuple(
            ScopeActivationTemplate(
                scope=scope,
                parent_scope=SCOPE_PARENT[scope],
                factories=tuple(factories_by_scope[scope]),
            )
            for scope in HOST_SCOPE_POLICIES[verified_host_plan.host_kind]
        ),
        integrity=IntegrityRecord("sha256", "0" * 64),
    )
    blueprint = replace(
        blueprint,
        integrity=_integrity_record(
            compute_host_activation_blueprint_integrity(blueprint)
        ),
    )
    output_diagnostics = validate_host_activation_blueprint_data(
        blueprint,
        validators,
    )
    if output_diagnostics:
        return _failure(output_diagnostics)
    return HostActivationBlueprintResult(blueprint=blueprint, diagnostics=())
