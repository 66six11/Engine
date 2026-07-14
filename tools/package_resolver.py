"""Deterministic in-memory package resolver for Asharia package contracts."""

from __future__ import annotations

import copy
import json
from dataclasses import dataclass
from functools import cmp_to_key
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools.package_candidates import PackageCandidate


RESOLVER_VERSION = "0.1.0"
RESOLUTION_POLICY_VERSION = 1


@dataclass(frozen=True)
class ResolverDiagnostic:
    """A deterministic resolution or input failure with requirement provenance."""

    code: str
    message: str
    identity: str = ""
    location: str = ""
    requirement_chains: tuple[tuple[str, ...], ...] = ()

    def render(self) -> str:
        prefix = f"{self.location}: " if self.location else ""
        identity = f" {self.identity}" if self.identity else ""
        rendered = f"{prefix}[{self.code}]{identity}: {self.message}"
        if self.requirement_chains:
            chains = "; ".join(" -> ".join(chain) for chain in self.requirement_chains)
            rendered += f"; required by: {chains}"
        return rendered


@dataclass(frozen=True)
class ResolutionResult:
    """Atomic resolver result: either a validated lock or diagnostics."""

    lock: dict[str, Any] | None
    selected_candidates: tuple[PackageCandidate, ...]
    diagnostics: tuple[ResolverDiagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.lock is not None and not self.diagnostics


@dataclass(frozen=True)
class _Requirement:
    identity: str
    package_kind: str
    constraint: dict[str, Any]
    chain: tuple[str, ...]


def _stable_json(value: Any) -> str:
    try:
        return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    except (TypeError, ValueError):
        return f"<{type(value).__name__}>"


def _candidate_input_key(candidate: PackageCandidate) -> tuple[str, ...]:
    return (
        str(candidate.identity),
        str(candidate.version),
        str(candidate.package_kind),
        str(candidate.origin),
        _stable_json(candidate.source),
        _stable_json(candidate.manifest_integrity),
        _stable_json(candidate.payload_integrity),
        _stable_json(candidate.manifest),
    )


def _diagnostic_sort_key(diagnostic: ResolverDiagnostic) -> tuple[Any, ...]:
    return (
        diagnostic.location,
        diagnostic.identity,
        diagnostic.code,
        diagnostic.message,
        diagnostic.requirement_chains,
    )


def _sorted_chains(requirements: Iterable[_Requirement]) -> tuple[tuple[str, ...], ...]:
    return tuple(
        sorted(
            {requirement.chain for requirement in requirements},
            key=lambda chain: (len(chain), chain),
        )
    )


def _constraint_text(constraint: dict[str, Any]) -> str:
    if constraint["kind"] == "exact":
        return f"== {constraint['version']}"
    prerelease = ", prerelease allowed" if constraint["allowPrerelease"] else ""
    return (
        f">= {constraint['minimumInclusive']}, "
        f"< {constraint['maximumExclusive']}{prerelease}"
    )


def _contract_diagnostic(
    diagnostic: contracts.Diagnostic,
    *,
    identity: str = "",
    chains: tuple[tuple[str, ...], ...] = (),
) -> ResolverDiagnostic:
    location = (
        f"{diagnostic.manifest_path}{diagnostic.pointer}"
        if diagnostic.pointer
        else diagnostic.manifest_path
    )
    return ResolverDiagnostic(
        code=diagnostic.code,
        identity=identity,
        location=location,
        message=diagnostic.message,
        requirement_chains=chains,
    )


def _validate_candidate_evidence(
    candidate: PackageCandidate,
    validators: contracts.ContractValidators,
) -> list[ResolverDiagnostic]:
    root_collection = (
        "directPackages"
        if candidate.package_kind == "installable-capability"
        else "directFeatureSets"
    )
    empty_collection = (
        "directFeatureSets"
        if root_collection == "directPackages"
        else "directPackages"
    )
    reference = {
        "id": candidate.identity,
        "version": candidate.version,
        "packageKind": candidate.package_kind,
    }
    node = {
        **reference,
        "source": candidate.source,
        "dependencies": [],
    }
    if not isinstance(candidate.source, dict) or candidate.source.get("kind") != (
        "engine-distribution"
    ):
        node["manifestIntegrity"] = candidate.manifest_integrity
        node["payloadIntegrity"] = candidate.payload_integrity
    lock = {
        "schema": "com.asharia.package-lock",
        "schemaVersion": 2,
        "resolver": {"version": RESOLVER_VERSION, "policyVersion": 1},
        "inputs": {
            "engine": {
                "distributionId": "com.asharia.distribution.placeholder",
                "engineApiVersion": "0.0.0",
                "engineGenerationId": f"sha256-{'0' * 64}",
            },
            "projectManifestIntegrity": {"algorithm": "sha256", "digest": "0" * 64},
        },
        "roots": {root_collection: [reference], empty_collection: []},
        "nodes": [node],
    }
    chain = ((f"candidate origin '{candidate.origin}'",),)
    diagnostics = contracts.validate_manifest_data(
        lock,
        f"{candidate.origin}/candidate-evidence",
        validators,
    )
    return [
        ResolverDiagnostic(
            code="resolver.candidate.evidence-invalid",
            identity=candidate.identity,
            location=(
                f"{diagnostic.manifest_path}{diagnostic.pointer}"
                if diagnostic.pointer
                else diagnostic.manifest_path
            ),
            message=f"candidate evidence violates [{diagnostic.code}]: {diagnostic.message}",
            requirement_chains=chain,
        )
        for diagnostic in diagnostics
    ]


def _validate_inputs(
    project: Any,
    distribution: Any,
    candidates: tuple[PackageCandidate, ...],
    validators: contracts.ContractValidators,
    resolver_version: str,
    policy_version: int,
) -> tuple[list[PackageCandidate], list[ResolverDiagnostic]]:
    diagnostics: list[ResolverDiagnostic] = []
    project_diagnostics = contracts.validate_manifest_data(
        project,
        contracts.PROJECT_MANIFEST_NAME,
        validators,
    )
    diagnostics.extend(_contract_diagnostic(item) for item in project_diagnostics)
    distribution_diagnostics = contracts.validate_manifest_data(
        distribution,
        contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
        validators,
    )
    diagnostics.extend(_contract_diagnostic(item) for item in distribution_diagnostics)

    try:
        contracts.compare_semantic_versions(resolver_version, resolver_version)
    except (TypeError, ValueError):
        diagnostics.append(
            ResolverDiagnostic(
                code="resolver.input.resolver-version-invalid",
                message=f"resolver version '{resolver_version}' is not Semantic Version",
            )
        )
    if type(policy_version) is not int or policy_version != RESOLUTION_POLICY_VERSION:
        diagnostics.append(
            ResolverDiagnostic(
                code="resolver.input.policy-version-unsupported",
                message=(
                    f"policy version {policy_version} is unsupported; "
                    f"expected {RESOLUTION_POLICY_VERSION}"
                ),
            )
        )

    normalized_distribution: dict[str, Any] | None = None
    inventory: dict[str, dict[str, Any]] = {}
    if not project_diagnostics and not distribution_diagnostics:
        normalized_distribution = contracts.normalize_engine_distribution_manifest(
            distribution
        )
        engine = normalized_distribution["distribution"]
        requirement = project["engine"]
        if requirement["distributionId"] != engine["id"]:
            diagnostics.append(
                ResolverDiagnostic(
                    code="resolver.engine.distribution-mismatch",
                    location=f"{contracts.PROJECT_MANIFEST_NAME}/engine/distributionId",
                    message=(
                        f"project requires Distribution '{requirement['distributionId']}' "
                        f"but current Distribution is '{engine['id']}'"
                    ),
                )
            )
        if not contracts.version_satisfies_constraint(
            engine["engineApiVersion"], requirement["apiVersion"]
        ):
            diagnostics.append(
                ResolverDiagnostic(
                    code="resolver.engine.api-incompatible",
                    location=f"{contracts.PROJECT_MANIFEST_NAME}/engine/apiVersion",
                    message=(
                        f"Engine API '{engine['engineApiVersion']}' does not satisfy the "
                        "project requirement"
                    ),
                )
            )
        inventory = {
            package["id"]: package
            for package in normalized_distribution["bundledPackages"]
        }

    valid_candidates: list[PackageCandidate] = []
    for candidate in sorted(candidates, key=_candidate_input_key):
        origin = candidate.origin if isinstance(candidate.origin, str) else "<invalid-origin>"
        candidate_path = f"{origin}/{contracts.PACKAGE_MANIFEST_NAME}"
        if not isinstance(candidate.origin, str) or not candidate.origin:
            diagnostics.append(
                ResolverDiagnostic(
                    code="resolver.candidate.origin-invalid",
                    message="candidate origin must be a non-empty stable string",
                    location=candidate_path,
                )
            )
        manifest_diagnostics = contracts.validate_manifest_data(
            candidate.manifest,
            candidate_path,
            validators,
        )
        diagnostics.extend(
            _contract_diagnostic(item, identity=str(candidate.identity))
            for item in manifest_diagnostics
        )
        if manifest_diagnostics:
            continue

        metadata_matches = True
        for attribute, actual, expected in (
            ("identity", candidate.identity, candidate.manifest["id"]),
            ("version", candidate.version, candidate.manifest["version"]),
            ("packageKind", candidate.package_kind, candidate.manifest["packageKind"]),
        ):
            if actual != expected:
                metadata_matches = False
                diagnostics.append(
                    ResolverDiagnostic(
                        code="resolver.candidate.metadata-mismatch",
                        identity=str(candidate.identity),
                        location=candidate_path,
                        message=(
                            f"candidate {attribute} '{actual}' does not match author manifest "
                            f"value '{expected}'"
                        ),
                        requirement_chains=((f"candidate origin '{origin}'",),),
                    )
                )
        if not metadata_matches:
            continue

        evidence_diagnostics = _validate_candidate_evidence(candidate, validators)
        diagnostics.extend(evidence_diagnostics)
        authority_valid = True
        inventory_package = inventory.get(candidate.identity)
        source_kind = (
            candidate.source.get("kind")
            if isinstance(candidate.source, dict)
            else None
        )
        if normalized_distribution is not None and inventory_package is not None:
            if source_kind != "engine-distribution":
                authority_valid = False
                diagnostics.append(
                    ResolverDiagnostic(
                        code="resolver.engine.distribution-shadowed",
                        identity=candidate.identity,
                        location=candidate_path,
                        message=(
                            f"project-owned source cannot shadow Engine Distribution package "
                            f"'{candidate.identity}'"
                        ),
                    )
                )
            elif (
                candidate.version != inventory_package["version"]
                or candidate.package_kind != inventory_package["packageKind"]
                or candidate.origin
                != f"engine-distribution:{inventory_package['root']}"
                or candidate.manifest_integrity
                != inventory_package["manifestIntegrity"]
                or candidate.payload_integrity != inventory_package["payloadIntegrity"]
            ):
                authority_valid = False
                diagnostics.append(
                    ResolverDiagnostic(
                        code="resolver.engine.distribution-candidate-mismatch",
                        identity=candidate.identity,
                        location=candidate_path,
                        message=(
                            "candidate metadata or bytes do not match the current Engine "
                            "Distribution inventory"
                        ),
                    )
                )
        elif normalized_distribution is not None and source_kind == "engine-distribution":
            authority_valid = False
            diagnostics.append(
                ResolverDiagnostic(
                    code="resolver.engine.package-not-distributed",
                    identity=candidate.identity,
                    location=candidate_path,
                    message=(
                        f"candidate '{candidate.identity}' is not present in the current "
                        "Engine Distribution inventory"
                    ),
                )
            )

        if not evidence_diagnostics and authority_valid:
            valid_candidates.append(candidate)

    kinds_by_identity: dict[str, set[str]] = {}
    for candidate in valid_candidates:
        kinds_by_identity.setdefault(candidate.identity, set()).add(candidate.package_kind)

    for identity, kinds in sorted(kinds_by_identity.items()):
        if len(kinds) > 1:
            diagnostics.append(
                ResolverDiagnostic(
                    code="resolver.candidate.identity-kind-conflict",
                    identity=identity,
                    message=(
                        "candidate identity is published with multiple kinds: "
                        + ", ".join(sorted(kinds))
                    ),
                    requirement_chains=tuple(
                        (f"candidate origin '{candidate.origin}'",)
                        for candidate in sorted(
                            (item for item in valid_candidates if item.identity == identity),
                            key=_candidate_input_key,
                        )
                    ),
                )
            )

    return valid_candidates, diagnostics


def _candidate_order(left: PackageCandidate, right: PackageCandidate) -> int:
    precedence = contracts.compare_semantic_versions(left.version, right.version)
    if precedence:
        return -precedence
    if left.version != right.version:
        return -1 if left.version < right.version else 1
    return -1 if left.origin < right.origin else (1 if left.origin > right.origin else 0)


def _requirements_for_candidate(
    candidate: PackageCandidate,
    parent_requirements: tuple[_Requirement, ...],
) -> tuple[_Requirement, ...]:
    parent_chain = min(
        (requirement.chain for requirement in parent_requirements),
        key=lambda chain: (len(chain), chain),
    )
    if candidate.package_kind == "installable-capability":
        declarations = (
            (requirement, "installable-capability")
            for requirement in candidate.manifest["dependencies"]
        )
    else:
        declarations = (
            *(
                (requirement, "installable-capability")
                for requirement in candidate.manifest["packages"]
            ),
            *(
                (requirement, "feature-set")
                for requirement in candidate.manifest["featureSets"]
            ),
        )
    requirements = []
    for declaration, package_kind in declarations:
        constraint = copy.deepcopy(declaration["version"])
        edge = (
            f"{candidate.identity}@{candidate.version} requires "
            f"{declaration['id']} {_constraint_text(constraint)}"
        )
        requirements.append(
            _Requirement(
                identity=declaration["id"],
                package_kind=package_kind,
                constraint=constraint,
                chain=parent_chain + (edge,),
            )
        )
    return tuple(sorted(requirements, key=_requirement_sort_key))


def _requirement_sort_key(requirement: _Requirement) -> tuple[Any, ...]:
    return (
        requirement.identity,
        requirement.package_kind,
        _stable_json(requirement.constraint),
        len(requirement.chain),
        requirement.chain,
    )


def _add_requirements(
    requirements: dict[str, tuple[_Requirement, ...]],
    additions: Iterable[_Requirement],
) -> dict[str, tuple[_Requirement, ...]]:
    result = dict(requirements)
    for requirement in additions:
        existing = list(result.get(requirement.identity, ()))
        key = _requirement_sort_key(requirement)
        if all(_requirement_sort_key(item) != key for item in existing):
            existing.append(requirement)
            existing.sort(key=_requirement_sort_key)
            result[requirement.identity] = tuple(existing)
    return result


def _requirements_are_satisfied(
    candidate: PackageCandidate,
    requirements: tuple[_Requirement, ...],
) -> bool:
    return all(
        candidate.package_kind == requirement.package_kind
        and contracts.version_satisfies_constraint(candidate.version, requirement.constraint)
        for requirement in requirements
    )


def _constraint_failure(
    identity: str,
    requirements: tuple[_Requirement, ...],
    selected: PackageCandidate | None,
    catalog: tuple[PackageCandidate, ...],
    engine_api_version: str,
) -> ResolverDiagnostic:
    chains = _sorted_chains(requirements)
    expected_kinds = sorted({requirement.package_kind for requirement in requirements})
    if len(expected_kinds) > 1:
        return ResolverDiagnostic(
            code="resolver.requirement.kind-conflict",
            identity=identity,
            message=f"requirements demand incompatible kinds: {', '.join(expected_kinds)}",
            requirement_chains=chains,
        )
    expected_kind = expected_kinds[0]
    if selected is not None:
        return ResolverDiagnostic(
            code=(
                "resolver.requirement.kind-mismatch"
                if selected.package_kind != expected_kind
                else "resolver.version.unsatisfied"
            ),
            identity=identity,
            message=(
                f"selected {selected.package_kind} {selected.version} no longer satisfies all "
                f"{expected_kind} requirements"
            ),
            requirement_chains=chains,
        )
    if not catalog:
        return ResolverDiagnostic(
            code="resolver.candidate.missing",
            identity=identity,
            message=f"no candidates were supplied for required kind '{expected_kind}'",
            requirement_chains=chains,
        )
    kind_candidates = tuple(item for item in catalog if item.package_kind == expected_kind)
    if not kind_candidates:
        available = ", ".join(sorted({item.package_kind for item in catalog}))
        return ResolverDiagnostic(
            code="resolver.requirement.kind-mismatch",
            identity=identity,
            message=f"required kind is '{expected_kind}', available kinds are: {available}",
            requirement_chains=chains,
        )
    version_candidates = tuple(
        candidate
        for candidate in kind_candidates
        if all(
            contracts.version_satisfies_constraint(candidate.version, requirement.constraint)
            for requirement in requirements
        )
    )
    if not version_candidates:
        available = ", ".join(candidate.version for candidate in kind_candidates)
        constraints = "; ".join(
            sorted({_constraint_text(requirement.constraint) for requirement in requirements})
        )
        return ResolverDiagnostic(
            code="resolver.version.unsatisfied",
            identity=identity,
            message=f"available versions [{available}] do not satisfy intersection [{constraints}]",
            requirement_chains=chains,
        )
    return ResolverDiagnostic(
        code="resolver.engine-api.unsatisfied",
        identity=identity,
        message=(
            f"version-compatible candidates do not support engine API '{engine_api_version}': "
            + ", ".join(candidate.version for candidate in version_candidates)
        ),
        requirement_chains=chains,
    )


def _search(
    catalog: dict[str, tuple[PackageCandidate, ...]],
    engine_api_version: str,
    requirements: dict[str, tuple[_Requirement, ...]],
    selected: dict[str, PackageCandidate],
) -> tuple[dict[str, PackageCandidate] | None, ResolverDiagnostic | None]:
    for identity in sorted(selected):
        identity_requirements = requirements.get(identity, ())
        if identity_requirements and not _requirements_are_satisfied(
            selected[identity], identity_requirements
        ):
            return None, _constraint_failure(
                identity,
                identity_requirements,
                selected[identity],
                catalog.get(identity, ()),
                engine_api_version,
            )

    unresolved = sorted(set(requirements) - set(selected))
    if not unresolved:
        return selected, None

    identity = unresolved[0]
    identity_requirements = requirements[identity]
    expected_kinds = {requirement.package_kind for requirement in identity_requirements}
    version_candidates = tuple(
        candidate
        for candidate in catalog.get(identity, ())
        if len(expected_kinds) == 1
        and candidate.package_kind in expected_kinds
        and all(
            contracts.version_satisfies_constraint(candidate.version, requirement.constraint)
            for requirement in identity_requirements
        )
    )
    if not version_candidates:
        return None, _constraint_failure(
            identity,
            identity_requirements,
            None,
            catalog.get(identity, ()),
            engine_api_version,
        )

    first_failure: ResolverDiagnostic | None = None
    candidates_by_version: dict[str, list[PackageCandidate]] = {}
    for candidate in version_candidates:
        candidates_by_version.setdefault(candidate.version, []).append(candidate)
    for version, exact_candidates_list in candidates_by_version.items():
        exact_candidates = tuple(exact_candidates_list)
        engine_compatible = tuple(
            candidate
            for candidate in exact_candidates
            if contracts.version_satisfies_constraint(
                engine_api_version,
                candidate.manifest["engineApi"],
            )
        )
        if not engine_compatible:
            continue
        if len(exact_candidates) > 1:
            sources = ", ".join(
                f"{item.origin}={_stable_json(item.source)}" for item in exact_candidates
            )
            return None, ResolverDiagnostic(
                code="resolver.candidate.ambiguous",
                identity=identity,
                message=f"version '{version}' has multiple eligible candidates: {sources}",
                requirement_chains=_sorted_chains(identity_requirements),
            )
        candidate = engine_compatible[0]
        branch_selected = dict(selected)
        branch_selected[identity] = candidate
        branch_requirements = _add_requirements(
            requirements,
            _requirements_for_candidate(candidate, identity_requirements),
        )
        solution, failure = _search(
            catalog,
            engine_api_version,
            branch_requirements,
            branch_selected,
        )
        if solution is not None:
            return solution, None
        if first_failure is None:
            first_failure = failure
    if first_failure is not None:
        return None, first_failure
    return None, _constraint_failure(
        identity,
        identity_requirements,
        None,
        catalog.get(identity, ()),
        engine_api_version,
    )


def _candidate_dependencies(
    candidate: PackageCandidate,
) -> tuple[tuple[str, str], ...]:
    if candidate.package_kind == "installable-capability":
        dependencies = (
            (requirement["id"], "installable-capability")
            for requirement in candidate.manifest["dependencies"]
        )
    else:
        dependencies = (
            *(
                (requirement["id"], "installable-capability")
                for requirement in candidate.manifest["packages"]
            ),
            *(
                (requirement["id"], "feature-set")
                for requirement in candidate.manifest["featureSets"]
            ),
        )
    return tuple(sorted(dependencies))


def _find_cycle(selected: dict[str, PackageCandidate]) -> tuple[str, ...] | None:
    state: dict[str, int] = {}
    stack: list[str] = []

    def visit(identity: str) -> tuple[str, ...] | None:
        state[identity] = 1
        stack.append(identity)
        for dependency, _ in _candidate_dependencies(selected[identity]):
            if dependency not in selected:
                continue
            if state.get(dependency, 0) == 0:
                cycle = visit(dependency)
                if cycle is not None:
                    return cycle
            elif state[dependency] == 1:
                start = stack.index(dependency)
                return tuple(stack[start:] + [dependency])
        stack.pop()
        state[identity] = 2
        return None

    for identity in sorted(selected):
        if state.get(identity, 0) == 0:
            cycle = visit(identity)
            if cycle is not None:
                return cycle
    return None


def _expand_selected_requirements(
    requirements: dict[str, tuple[_Requirement, ...]],
    selected: dict[str, PackageCandidate],
) -> dict[str, tuple[_Requirement, ...]]:
    """Rebuild provenance for a solved graph without depending on search stack state."""

    result = dict(requirements)
    expanded: set[str] = set()
    while True:
        ready = sorted(set(selected) & set(result) - expanded)
        if not ready:
            return result
        for identity in ready:
            result = _add_requirements(
                result,
                _requirements_for_candidate(selected[identity], result[identity]),
            )
            expanded.add(identity)


def _exact_reference(candidate: PackageCandidate) -> dict[str, Any]:
    return {
        "id": candidate.identity,
        "version": candidate.version,
        "packageKind": candidate.package_kind,
    }


def _materialize_lock(
    project: dict[str, Any],
    distribution: dict[str, Any],
    selected: dict[str, PackageCandidate],
    resolver_version: str,
    policy_version: int,
) -> dict[str, Any]:
    nodes = []
    for identity in sorted(selected):
        candidate = selected[identity]
        node = {
            **_exact_reference(candidate),
            "source": copy.deepcopy(candidate.source),
            "dependencies": [
                _exact_reference(selected[dependency])
                for dependency, _ in _candidate_dependencies(candidate)
            ],
        }
        if candidate.source["kind"] != "engine-distribution":
            node["manifestIntegrity"] = copy.deepcopy(candidate.manifest_integrity)
            node["payloadIntegrity"] = copy.deepcopy(candidate.payload_integrity)
        nodes.append(node)

    engine = distribution["distribution"]
    lock = {
        "schema": "com.asharia.package-lock",
        "schemaVersion": 2,
        "resolver": {"version": resolver_version, "policyVersion": policy_version},
        "inputs": {
            "engine": {
                "distributionId": engine["id"],
                "engineApiVersion": engine["engineApiVersion"],
                "engineGenerationId": distribution["engineGenerationId"],
            },
            "projectManifestIntegrity": contracts.compute_project_manifest_integrity(project),
        },
        "roots": {
            "directPackages": [
                _exact_reference(selected[requirement["id"]])
                for requirement in project["directPackages"]
            ],
            "directFeatureSets": [
                _exact_reference(selected[requirement["id"]])
                for requirement in project["directFeatureSets"]
            ],
        },
        "nodes": nodes,
    }
    return contracts.normalize_lock_manifest(lock)


def resolve_package_graph(
    project: Any,
    distribution: Any,
    candidates: Iterable[PackageCandidate],
    validators: contracts.ContractValidators,
    *,
    resolver_version: str = RESOLVER_VERSION,
    policy_version: int = RESOLUTION_POLICY_VERSION,
) -> ResolutionResult:
    """Resolve caller-provided candidates into one validated canonical lock graph."""

    candidate_snapshot = tuple(candidates)
    valid_candidates, diagnostics = _validate_inputs(
        project,
        distribution,
        candidate_snapshot,
        validators,
        resolver_version,
        policy_version,
    )
    if diagnostics:
        return ResolutionResult(
            lock=None,
            selected_candidates=(),
            diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
        )

    normalized_distribution = contracts.normalize_engine_distribution_manifest(distribution)
    engine_api_version = normalized_distribution["distribution"]["engineApiVersion"]

    catalog_lists: dict[str, list[PackageCandidate]] = {}
    for candidate in valid_candidates:
        catalog_lists.setdefault(candidate.identity, []).append(candidate)
    catalog = {
        identity: tuple(sorted(items, key=cmp_to_key(_candidate_order)))
        for identity, items in catalog_lists.items()
    }

    requirements: dict[str, tuple[_Requirement, ...]] = {}
    root_requirements = []
    for collection, package_kind in (
        ("directPackages", "installable-capability"),
        ("directFeatureSets", "feature-set"),
    ):
        for requirement in project[collection]:
            constraint = copy.deepcopy(requirement["version"])
            root_requirements.append(
                _Requirement(
                    identity=requirement["id"],
                    package_kind=package_kind,
                    constraint=constraint,
                    chain=(
                        f"project.{collection} requires {requirement['id']} "
                        f"{_constraint_text(constraint)}",
                    ),
                )
            )
    requirements = _add_requirements(requirements, root_requirements)
    selected, failure = _search(catalog, engine_api_version, requirements, {})
    if selected is None:
        assert failure is not None
        return ResolutionResult(lock=None, selected_candidates=(), diagnostics=(failure,))

    resolved_requirements = _expand_selected_requirements(requirements, selected)
    cycle = _find_cycle(selected)
    if cycle is not None:
        cycle_requirements = resolved_requirements.get(cycle[0], ())
        return ResolutionResult(
            lock=None,
            selected_candidates=(),
            diagnostics=(
                ResolverDiagnostic(
                    code="resolver.dependency.cycle",
                    identity=cycle[0],
                    message=f"selected dependency cycle: {' -> '.join(cycle)}",
                    requirement_chains=_sorted_chains(cycle_requirements),
                ),
            ),
        )

    lock = _materialize_lock(
        project,
        normalized_distribution,
        selected,
        resolver_version,
        policy_version,
    )
    selected_candidates = tuple(selected[identity] for identity in sorted(selected))
    output_diagnostics = contracts.validate_locked_result_data(
        lock,
        project,
        [candidate.manifest for candidate in selected_candidates],
        validators,
    )
    if output_diagnostics:
        return ResolutionResult(
            lock=None,
            selected_candidates=(),
            diagnostics=tuple(
                sorted(
                    (_contract_diagnostic(item) for item in output_diagnostics),
                    key=_diagnostic_sort_key,
                )
            ),
        )
    return ResolutionResult(
        lock=lock,
        selected_candidates=selected_candidates,
        diagnostics=(),
    )
