"""Deterministic, no-write Package Lock update planning.

The planner compares one validated base Project/Lock with proposed Project
intent, resolves only from a caller-provided complete candidate snapshot, and
returns either one immutable plan or stable diagnostics.  Filesystem discovery,
acquisition, persistence, and apply/recovery are deliberately outside this
module.
"""

from __future__ import annotations

import copy
import json
import re
from collections import Counter
from dataclasses import dataclass, field
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import package_resolver
from tools.package_candidates import PackageCandidate


UPDATE_POLICY_VERSION = 1
FULL_UPDATE_MODE = "full"
TARGETED_CONSERVATIVE_MODE = "targeted-conservative"
_UPDATE_MODES = (FULL_UPDATE_MODE, TARGETED_CONSERVATIVE_MODE)
_PACKAGE_ID_PATTERN = re.compile(
    r"^[a-z][a-z0-9]*(?:\.[a-z](?:[a-z0-9-]*[a-z0-9])?){2,}$"
)
_WINDOWS_ABSOLUTE_PATH_PATTERN = re.compile(
    r"(?:[A-Za-z]:[\\/]|\\\\(?:\?\\)?[^\\\r\n'\";]+[\\/])[^'\";\r\n]*"
)
_POSIX_ABSOLUTE_PATH_PATTERN = re.compile(
    r"(?:^|(?<=[\s'\"(]))/[^'\";\r\n]*"
)
_CHANGE_ORDER = {
    "added": 0,
    "removed": 1,
    "upgraded": 2,
    "downgraded": 3,
    "version-changed": 4,
    "source-changed": 5,
    "dependencies-changed": 6,
    "evidence-refreshed": 7,
    "became-direct": 8,
    "became-transitive": 9,
    "direct-requirement-changed": 10,
    "package-options-changed": 11,
}


@dataclass(frozen=True)
class LockUpdateRequest:
    """One explicit update policy selection."""

    mode: str
    unlock_targets: tuple[str, ...] = ()
    intent_only_targets: tuple[str, ...] = ()


@dataclass(frozen=True)
class IntegrityRecord:
    algorithm: str
    digest: str

    def to_data(self) -> dict[str, str]:
        return {"algorithm": self.algorithm, "digest": self.digest}


@dataclass(frozen=True)
class ExactPackageReference:
    identity: str
    version: str
    package_kind: str

    def to_data(self) -> dict[str, str]:
        return {
            "id": self.identity,
            "version": self.version,
            "packageKind": self.package_kind,
        }


@dataclass(frozen=True)
class LockedPackageSnapshot:
    """Immutable logical projection of one normalized Lock node."""

    identity: str
    version: str
    package_kind: str
    _source_json: str = field(repr=False)
    dependencies: tuple[ExactPackageReference, ...]
    manifest_integrity: IntegrityRecord | None = None
    payload_integrity: IntegrityRecord | None = None

    @property
    def source(self) -> dict[str, Any]:
        value = json.loads(self._source_json)
        assert isinstance(value, dict)
        return value

    def to_data(self) -> dict[str, Any]:
        value: dict[str, Any] = {
            "id": self.identity,
            "version": self.version,
            "packageKind": self.package_kind,
            "source": self.source,
            "dependencies": [item.to_data() for item in self.dependencies],
        }
        if self.manifest_integrity is not None:
            value["manifestIntegrity"] = self.manifest_integrity.to_data()
        if self.payload_integrity is not None:
            value["payloadIntegrity"] = self.payload_integrity.to_data()
        return value


@dataclass(frozen=True)
class PackageLockUpdateImpact:
    """One stable graph change between the base and proposed Lock."""

    identity: str
    package_kind: str
    cause: str
    changes: tuple[str, ...]
    before: LockedPackageSnapshot | None
    after: LockedPackageSnapshot | None

    def to_data(self) -> dict[str, Any]:
        value: dict[str, Any] = {
            "id": self.identity,
            "packageKind": self.package_kind,
            "cause": self.cause,
            "changes": list(self.changes),
        }
        if self.before is not None:
            value["before"] = self.before.to_data()
        if self.after is not None:
            value["after"] = self.after.to_data()
        return value


@dataclass(frozen=True)
class PackageLockUpdatePlan:
    """Detached no-write plan bound to all semantic inputs and outputs."""

    mode: str
    policy_version: int
    resolver_policy_version: int
    unlock_targets: tuple[str, ...]
    intent_only_targets: tuple[str, ...]
    status: str
    project_manifest_changed: bool
    engine_input_changed: bool
    impacts: tuple[PackageLockUpdateImpact, ...]
    base_project_integrity: IntegrityRecord
    base_lock_integrity: IntegrityRecord
    proposed_project_integrity: IntegrityRecord
    distribution_integrity: IntegrityRecord
    candidate_set_integrity: IntegrityRecord
    request_integrity: IntegrityRecord
    selected_candidate_set_integrity: IntegrityRecord
    proposed_lock_integrity: IntegrityRecord
    impact_set_integrity: IntegrityRecord
    plan_integrity: IntegrityRecord
    engine_generation_id: str
    _base_project_bytes: bytes = field(repr=False)
    _base_lock_bytes: bytes = field(repr=False)
    _proposed_project_bytes: bytes = field(repr=False)
    _proposed_lock_bytes: bytes = field(repr=False)
    _selected_candidates: tuple[PackageCandidate, ...] = field(repr=False)

    @property
    def base_project(self) -> dict[str, Any]:
        return _decode_object(self._base_project_bytes)

    @property
    def base_lock(self) -> dict[str, Any]:
        return _decode_object(self._base_lock_bytes)

    @property
    def proposed_project(self) -> dict[str, Any]:
        return _decode_object(self._proposed_project_bytes)

    @property
    def proposed_lock(self) -> dict[str, Any]:
        return _decode_object(self._proposed_lock_bytes)

    @property
    def selected_candidates(self) -> tuple[PackageCandidate, ...]:
        return copy.deepcopy(self._selected_candidates)


@dataclass(frozen=True)
class PackageLockUpdateDiagnostic:
    """Stable planning failure with requirement provenance and no machine paths."""

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
class PackageLockUpdatePlanResult:
    """Atomic result: one complete plan or deterministic diagnostics."""

    plan: PackageLockUpdatePlan | None
    diagnostics: tuple[PackageLockUpdateDiagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.plan is not None and not self.diagnostics


def _stable_json(value: Any) -> str:
    try:
        return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    except (TypeError, ValueError):
        return f"<{type(value).__name__}>"


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8", errors="surrogatepass")


def _redact(value: str) -> str:
    redacted = _WINDOWS_ABSOLUTE_PATH_PATTERN.sub("<redacted-path>", value)
    return _POSIX_ABSOLUTE_PATH_PATTERN.sub("<redacted-path>", redacted)


def _safe_identity(value: Any) -> str:
    return _redact(str(value))


def _diagnostic_sort_key(
    value: PackageLockUpdateDiagnostic,
) -> tuple[bytes, str, str, str, tuple[tuple[str, ...], ...]]:
    return (
        _utf8_key(value.identity),
        value.location,
        value.code,
        value.message,
        value.requirement_chains,
    )


def _ordered_diagnostics(
    values: Iterable[PackageLockUpdateDiagnostic],
) -> tuple[PackageLockUpdateDiagnostic, ...]:
    unique = {
        (
            value.code,
            value.message,
            value.identity,
            value.location,
            value.requirement_chains,
        ): value
        for value in values
    }
    return tuple(sorted(unique.values(), key=_diagnostic_sort_key))


def _failure(
    diagnostics: Iterable[PackageLockUpdateDiagnostic],
) -> PackageLockUpdatePlanResult:
    return PackageLockUpdatePlanResult(
        plan=None,
        diagnostics=_ordered_diagnostics(diagnostics),
    )


def _from_contract_diagnostic(
    value: contracts.Diagnostic,
) -> PackageLockUpdateDiagnostic:
    location = (
        f"{value.manifest_path}{value.pointer}"
        if value.pointer
        else value.manifest_path
    )
    return PackageLockUpdateDiagnostic(
        code=value.code,
        message=_redact(value.message),
        location=_redact(location),
    )


def _from_resolver_diagnostic(
    value: package_resolver.ResolverDiagnostic,
) -> PackageLockUpdateDiagnostic:
    try:
        raw_chains = tuple(value.requirement_chains)
        chains = tuple(
            tuple(_redact(str(part)) for part in tuple(chain))
            for chain in raw_chains
        )
    except Exception:
        chains = ()
    return PackageLockUpdateDiagnostic(
        code=str(value.code),
        message=_redact(str(value.message)),
        identity=_safe_identity(value.identity),
        location=_redact(str(value.location)),
        requirement_chains=chains,
    )


def _integrity(value: dict[str, str]) -> IntegrityRecord:
    return IntegrityRecord(value["algorithm"], value["digest"])


def _domain_integrity(domain: str, value: bytes) -> IntegrityRecord:
    framed = domain.encode("utf-8") + b"\0" + value
    return _integrity(contracts.compute_bytes_integrity(framed))


def _decode_object(value: bytes) -> dict[str, Any]:
    parsed = json.loads(value.decode("utf-8"))
    assert isinstance(parsed, dict)
    return parsed


def _project_bytes(value: dict[str, Any]) -> bytes:
    return contracts.render_normalized_project_manifest(value).encode("utf-8")


def _lock_bytes(value: dict[str, Any]) -> bytes:
    return contracts.render_normalized_lock_manifest(value).encode("utf-8")


def _distribution_bytes(value: dict[str, Any]) -> bytes:
    return contracts.render_normalized_engine_distribution_manifest(value).encode("utf-8")


def _optional_bytes_integrity(value: bytes | None) -> dict[str, str] | None:
    if value is None:
        return None
    if not isinstance(value, bytes):
        raise TypeError("candidate contract bytes must be bytes or None")
    return contracts.compute_bytes_integrity(value)


def _candidate_projection(candidate: PackageCandidate) -> dict[str, Any]:
    return {
        "id": candidate.identity,
        "version": candidate.version,
        "packageKind": candidate.package_kind,
        "source": copy.deepcopy(candidate.source),
        "manifestIntegrity": copy.deepcopy(candidate.manifest_integrity),
        "payloadIntegrity": copy.deepcopy(candidate.payload_integrity),
        "manifest": copy.deepcopy(candidate.manifest),
        "buildDescriptor": copy.deepcopy(candidate.build_descriptor),
        "buildDescriptorIntegrity": copy.deepcopy(
            candidate.build_descriptor_integrity
        ),
        "buildDescriptorBytesIntegrity": _optional_bytes_integrity(
            candidate.build_descriptor_bytes
        ),
        "productDeclaration": copy.deepcopy(candidate.product_declaration),
        "productDeclarationIntegrity": copy.deepcopy(
            candidate.product_declaration_integrity
        ),
        "productDeclarationBytesIntegrity": _optional_bytes_integrity(
            candidate.product_declaration_bytes
        ),
        "factoryDeclaration": copy.deepcopy(candidate.factory_declaration),
        "factoryDeclarationIntegrity": copy.deepcopy(
            candidate.factory_declaration_integrity
        ),
        "factoryDeclarationBytesIntegrity": _optional_bytes_integrity(
            candidate.factory_declaration_bytes
        ),
        "staticFactoryBindings": copy.deepcopy(candidate.static_factory_bindings),
        "staticFactoryBindingsIntegrity": copy.deepcopy(
            candidate.static_factory_bindings_integrity
        ),
        "staticFactoryBindingsBytesIntegrity": _optional_bytes_integrity(
            candidate.static_factory_bindings_bytes
        ),
    }


def _candidate_projection_key(candidate: PackageCandidate) -> str:
    return json.dumps(
        _candidate_projection(candidate),
        allow_nan=False,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    )


def _candidate_set_bytes(candidates: tuple[PackageCandidate, ...]) -> bytes:
    projection = sorted(
        (_candidate_projection(candidate) for candidate in candidates),
        key=lambda value: _utf8_key(
            json.dumps(
                value,
                allow_nan=False,
                ensure_ascii=False,
                sort_keys=True,
                separators=(",", ":"),
            )
        ),
    )
    return (
        json.dumps(
            projection,
            allow_nan=False,
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")


def _portable_candidate_origin(candidate: PackageCandidate) -> str:
    source = candidate.source
    if not isinstance(source, dict):
        return "detached:candidate"
    kind = source.get("kind")
    if kind == "project-embedded" and isinstance(source.get("relativePath"), str):
        return f"project-embedded:{source['relativePath']}"
    if kind == "local" and isinstance(source.get("sourceId"), str):
        return f"local:{source['sourceId']}"
    if kind == "engine-distribution":
        return "engine-distribution:detached"
    return "detached:candidate"


def _resolver_candidate_origin(candidate: PackageCandidate) -> Any:
    if isinstance(candidate.source, dict) and candidate.source.get("kind") == (
        "engine-distribution"
    ):
        return copy.deepcopy(candidate.origin)
    return _portable_candidate_origin(candidate)


def _capture_candidate(
    candidate: PackageCandidate,
    *,
    portable_origin: bool = False,
    resolver_origin: bool = False,
) -> PackageCandidate:
    """Detach every resolver-visible field and deliberately omit physical payload paths."""

    return PackageCandidate(
        identity=copy.deepcopy(candidate.identity),
        version=copy.deepcopy(candidate.version),
        package_kind=copy.deepcopy(candidate.package_kind),
        origin=(
            _resolver_candidate_origin(candidate)
            if resolver_origin
            else (
                _portable_candidate_origin(candidate)
                if portable_origin
                else copy.deepcopy(candidate.origin)
            )
        ),
        source=copy.deepcopy(candidate.source),
        manifest_integrity=copy.deepcopy(candidate.manifest_integrity),
        payload_integrity=copy.deepcopy(candidate.payload_integrity),
        manifest=copy.deepcopy(candidate.manifest),
        build_descriptor=copy.deepcopy(candidate.build_descriptor),
        build_descriptor_integrity=copy.deepcopy(
            candidate.build_descriptor_integrity
        ),
        build_descriptor_bytes=copy.deepcopy(candidate.build_descriptor_bytes),
        product_declaration=copy.deepcopy(candidate.product_declaration),
        product_declaration_integrity=copy.deepcopy(
            candidate.product_declaration_integrity
        ),
        product_declaration_bytes=copy.deepcopy(candidate.product_declaration_bytes),
        factory_declaration=copy.deepcopy(candidate.factory_declaration),
        factory_declaration_integrity=copy.deepcopy(
            candidate.factory_declaration_integrity
        ),
        factory_declaration_bytes=copy.deepcopy(candidate.factory_declaration_bytes),
        static_factory_bindings=copy.deepcopy(candidate.static_factory_bindings),
        static_factory_bindings_integrity=copy.deepcopy(
            candidate.static_factory_bindings_integrity
        ),
        static_factory_bindings_bytes=copy.deepcopy(
            candidate.static_factory_bindings_bytes
        ),
        payload_location=None,
    )


def _validate_request(
    request: object,
) -> tuple[LockUpdateRequest | None, tuple[PackageLockUpdateDiagnostic, ...]]:
    if not isinstance(request, LockUpdateRequest):
        return None, (
            PackageLockUpdateDiagnostic(
                code="update.request.invalid",
                message="update request must use LockUpdateRequest",
                location="request",
            ),
        )
    diagnostics: list[PackageLockUpdateDiagnostic] = []
    if request.mode not in _UPDATE_MODES:
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.request.mode-unsupported",
                message=f"update mode must be one of: {', '.join(_UPDATE_MODES)}",
                location="request/mode",
            )
        )
    def capture_targets(
        values: object,
        field_name: str,
    ) -> tuple[str, ...]:
        location = f"request/{field_name}"
        if type(values) is not tuple:
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.request.targets-invalid",
                    message=f"{field_name} must be an immutable tuple",
                    location=location,
                )
            )
            return ()
        valid: list[str] = []
        for index, target in enumerate(values):
            if not isinstance(target, str) or not _PACKAGE_ID_PATTERN.fullmatch(target):
                diagnostics.append(
                    PackageLockUpdateDiagnostic(
                        code="update.request.target-invalid",
                        message="target must be a canonical package identity",
                        location=f"{location}/{index}",
                    )
                )
            else:
                valid.append(target)
        if len(set(valid)) != len(valid):
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.request.target-duplicate",
                    message=f"{field_name} identities must be unique",
                    location=location,
                )
            )
        canonical = tuple(sorted(valid, key=_utf8_key))
        if tuple(valid) != canonical:
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.request.targets-noncanonical",
                    message=f"{field_name} must be sorted by UTF-8 bytes",
                    location=location,
                )
            )
        return canonical

    unlock_targets = capture_targets(request.unlock_targets, "unlockTargets")
    intent_only_targets = capture_targets(
        request.intent_only_targets,
        "intentOnlyTargets",
    )
    overlap = sorted(set(unlock_targets) & set(intent_only_targets), key=_utf8_key)
    for identity in overlap:
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.request.target-role-conflict",
                identity=identity,
                message="one identity cannot be both unlocked and intent-only",
                location="request",
            )
        )
    if request.mode == FULL_UPDATE_MODE and (unlock_targets or intent_only_targets):
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.request.full-targets-forbidden",
                message="full update does not accept target identities",
                location="request",
            )
        )
    if (
        request.mode == TARGETED_CONSERVATIVE_MODE
        and not unlock_targets
        and not intent_only_targets
    ):
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.request.targets-required",
                message="targeted-conservative update requires at least one target",
                location="request",
            )
        )
    if diagnostics:
        return None, _ordered_diagnostics(diagnostics)
    return LockUpdateRequest(request.mode, unlock_targets, intent_only_targets), ()


def _direct_requirements_by_identity(
    project: dict[str, Any],
) -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    for collection, kind in (
        ("directPackages", "installable-capability"),
        ("directFeatureSets", "feature-set"),
    ):
        for requirement in project[collection]:
            result[requirement["id"]] = (kind, _stable_json(requirement["version"]))
    return result


def _package_options_by_identity(project: dict[str, Any]) -> dict[str, str]:
    result: dict[str, str] = {}
    for option_group in project["packageOptions"]:
        result[option_group["packageId"]] = _stable_json(option_group["values"])
    return result


def _changed_mapping_identities(
    before: dict[str, Any],
    after: dict[str, Any],
) -> tuple[str, ...]:
    return tuple(
        sorted(
            (
                identity
                for identity in set(before) | set(after)
                if before.get(identity) != after.get(identity)
            ),
            key=_utf8_key,
        )
    )


def _source_equal(left: Any, right: Any) -> bool:
    return _stable_json(left) == _stable_json(right)


def _candidate_matches_locked_node(
    candidate: PackageCandidate,
    node: dict[str, Any],
) -> bool:
    matches = (
        candidate.identity == node["id"]
        and candidate.version == node["version"]
        and candidate.package_kind == node["packageKind"]
        and _source_equal(candidate.source, node["source"])
    )
    if not matches:
        return False
    if node["source"].get("kind") == "engine-distribution":
        return True
    return (
        candidate.manifest_integrity == node.get("manifestIntegrity")
        and candidate.payload_integrity == node.get("payloadIntegrity")
    )


def _preference_from_node(node: dict[str, Any]) -> package_resolver.CandidatePreference:
    evidence_required = node["source"].get("kind") != "engine-distribution"
    return package_resolver.CandidatePreference.capture(
        identity=node["id"],
        version=node["version"],
        package_kind=node["packageKind"],
        source=node["source"],
        manifest_integrity=(node.get("manifestIntegrity") if evidence_required else None),
        payload_integrity=(node.get("payloadIntegrity") if evidence_required else None),
    )


def _current_engine_input(distribution: dict[str, Any]) -> dict[str, str]:
    engine = distribution["distribution"]
    return {
        "distributionId": engine["id"],
        "engineApiVersion": engine["engineApiVersion"],
        "engineGenerationId": distribution["engineGenerationId"],
    }


def _snapshot_node(node: dict[str, Any]) -> LockedPackageSnapshot:
    dependencies = tuple(
        ExactPackageReference(
            identity=value["id"],
            version=value["version"],
            package_kind=value["packageKind"],
        )
        for value in node["dependencies"]
    )
    manifest_integrity = (
        _integrity(node["manifestIntegrity"])
        if "manifestIntegrity" in node
        else None
    )
    payload_integrity = (
        _integrity(node["payloadIntegrity"])
        if "payloadIntegrity" in node
        else None
    )
    return LockedPackageSnapshot(
        identity=node["id"],
        version=node["version"],
        package_kind=node["packageKind"],
        _source_json=_stable_json(node["source"]),
        dependencies=dependencies,
        manifest_integrity=manifest_integrity,
        payload_integrity=payload_integrity,
    )


def _locked_node_changes(
    before: LockedPackageSnapshot | None,
    after: LockedPackageSnapshot | None,
) -> tuple[str, ...]:
    if before is None:
        return ("added",)
    if after is None:
        return ("removed",)
    changes: list[str] = []
    comparison = contracts.compare_semantic_versions(after.version, before.version)
    if comparison > 0:
        changes.append("upgraded")
    elif comparison < 0:
        changes.append("downgraded")
    elif before.version != after.version:
        changes.append("version-changed")
    if before._source_json != after._source_json:
        changes.append("source-changed")
    if before.dependencies != after.dependencies:
        changes.append("dependencies-changed")
    if (
        before.manifest_integrity != after.manifest_integrity
        or before.payload_integrity != after.payload_integrity
    ):
        changes.append("evidence-refreshed")
    return tuple(sorted(changes, key=_CHANGE_ORDER.__getitem__))


def _derive_impacts(
    base_project: dict[str, Any],
    proposed_project: dict[str, Any],
    base_lock: dict[str, Any],
    proposed_lock: dict[str, Any],
    request: LockUpdateRequest,
) -> tuple[PackageLockUpdateImpact, ...]:
    before_nodes = {node["id"]: _snapshot_node(node) for node in base_lock["nodes"]}
    after_nodes = {node["id"]: _snapshot_node(node) for node in proposed_lock["nodes"]}
    before_direct = _direct_requirements_by_identity(base_project)
    after_direct = _direct_requirements_by_identity(proposed_project)
    before_options = _package_options_by_identity(base_project)
    after_options = _package_options_by_identity(proposed_project)
    direct_changes = set(_changed_mapping_identities(before_direct, after_direct))
    option_changes = set(_changed_mapping_identities(before_options, after_options))
    impacts: list[PackageLockUpdateImpact] = []
    identities = (
        set(before_nodes)
        | set(after_nodes)
        | direct_changes
        | option_changes
    )
    for identity in sorted(identities, key=_utf8_key):
        before = before_nodes.get(identity)
        after = after_nodes.get(identity)
        changes = list(_locked_node_changes(before, after))
        was_direct = identity in before_direct
        is_direct = identity in after_direct
        if not was_direct and is_direct:
            changes.append("became-direct")
        elif was_direct and not is_direct and after is not None:
            changes.append("became-transitive")
        if identity in direct_changes:
            changes.append("direct-requirement-changed")
        if identity in option_changes:
            changes.append("package-options-changed")
        if not changes:
            continue
        changes = sorted(set(changes), key=_CHANGE_ORDER.__getitem__)
        if after is not None:
            package_kind = after.package_kind
        elif before is not None:
            package_kind = before.package_kind
        else:
            package_kind = (after_direct.get(identity) or before_direct[identity])[0]
        if request.mode == FULL_UPDATE_MODE:
            cause = "full-policy"
        elif identity in request.unlock_targets:
            cause = "requested-target"
        elif (
            identity in request.intent_only_targets
            or identity in direct_changes
            or identity in option_changes
        ):
            cause = "direct-intent"
        else:
            cause = "transitive"
        impacts.append(
            PackageLockUpdateImpact(
                identity=identity,
                package_kind=package_kind,
                cause=cause,
                changes=tuple(changes),
                before=before,
                after=after,
            )
        )
    return tuple(impacts)


def _kind_changed_identities(
    base_lock: dict[str, Any],
    proposed_lock: dict[str, Any],
) -> tuple[str, ...]:
    before = {node["id"]: node["packageKind"] for node in base_lock["nodes"]}
    after = {node["id"]: node["packageKind"] for node in proposed_lock["nodes"]}
    return tuple(
        identity
        for identity in sorted(set(before) & set(after), key=_utf8_key)
        if before[identity] != after[identity]
    )


def _preview_payload(plan: PackageLockUpdatePlan) -> dict[str, Any]:
    return {
        "policy": {
            "mode": plan.mode,
            "policyVersion": plan.policy_version,
            "resolverPolicyVersion": plan.resolver_policy_version,
            "unlockTargets": list(plan.unlock_targets),
            "intentOnlyTargets": list(plan.intent_only_targets),
            "requestIntegrity": plan.request_integrity.to_data(),
        },
        "status": plan.status,
        "inputs": {
            "baseProjectManifestIntegrity": plan.base_project_integrity.to_data(),
            "baseLockIntegrity": plan.base_lock_integrity.to_data(),
            "proposedProjectManifestIntegrity": plan.proposed_project_integrity.to_data(),
            "distributionIntegrity": plan.distribution_integrity.to_data(),
            "engineGenerationId": plan.engine_generation_id,
            "candidateSetIntegrity": plan.candidate_set_integrity.to_data(),
        },
        "outputs": {
            "proposedLockIntegrity": plan.proposed_lock_integrity.to_data(),
            "selectedCandidateSetIntegrity": (
                plan.selected_candidate_set_integrity.to_data()
            ),
            "impactSetIntegrity": plan.impact_set_integrity.to_data(),
            "projectManifestChanged": plan.project_manifest_changed,
            "engineInputChanged": plan.engine_input_changed,
        },
        "impacts": [impact.to_data() for impact in plan.impacts],
    }


def package_lock_update_preview_data(plan: PackageLockUpdatePlan) -> dict[str, Any]:
    """Return a detached, path-redacted public preview projection."""

    value = _preview_payload(plan)
    value["planIntegrity"] = plan.plan_integrity.to_data()
    return value


def render_package_lock_update_preview(plan: PackageLockUpdatePlan) -> str:
    """Render canonical UTF-8-compatible preview JSON with LF and final newline."""

    return json.dumps(
        package_lock_update_preview_data(plan),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def _plan_integrity_payload(
    *,
    mode: str,
    resolver_policy_version: int,
    unlock_targets: tuple[str, ...],
    intent_only_targets: tuple[str, ...],
    status: str,
    project_manifest_changed: bool,
    engine_input_changed: bool,
    impacts: tuple[PackageLockUpdateImpact, ...],
    base_project_integrity: IntegrityRecord,
    base_lock_integrity: IntegrityRecord,
    proposed_project_integrity: IntegrityRecord,
    distribution_integrity: IntegrityRecord,
    candidate_set_integrity: IntegrityRecord,
    request_integrity: IntegrityRecord,
    selected_candidate_set_integrity: IntegrityRecord,
    proposed_lock_integrity: IntegrityRecord,
    impact_set_integrity: IntegrityRecord,
    engine_generation_id: str,
) -> bytes:
    value = {
        "policy": {
            "mode": mode,
            "policyVersion": UPDATE_POLICY_VERSION,
            "resolverPolicyVersion": resolver_policy_version,
            "unlockTargets": list(unlock_targets),
            "intentOnlyTargets": list(intent_only_targets),
            "requestIntegrity": request_integrity.to_data(),
        },
        "status": status,
        "inputs": {
            "baseProjectManifestIntegrity": base_project_integrity.to_data(),
            "baseLockIntegrity": base_lock_integrity.to_data(),
            "proposedProjectManifestIntegrity": proposed_project_integrity.to_data(),
            "distributionIntegrity": distribution_integrity.to_data(),
            "engineGenerationId": engine_generation_id,
            "candidateSetIntegrity": candidate_set_integrity.to_data(),
        },
        "outputs": {
            "proposedLockIntegrity": proposed_lock_integrity.to_data(),
            "selectedCandidateSetIntegrity": (
                selected_candidate_set_integrity.to_data()
            ),
            "impactSetIntegrity": impact_set_integrity.to_data(),
            "projectManifestChanged": project_manifest_changed,
            "engineInputChanged": engine_input_changed,
        },
        "impacts": [impact.to_data() for impact in impacts],
    }
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def _request_bytes(
    request: LockUpdateRequest,
    resolver_policy_version: int,
) -> bytes:
    value = {
        "mode": request.mode,
        "policyVersion": UPDATE_POLICY_VERSION,
        "resolverPolicyVersion": resolver_policy_version,
        "unlockTargets": list(request.unlock_targets),
        "intentOnlyTargets": list(request.intent_only_targets),
    }
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def _impact_set_bytes(impacts: tuple[PackageLockUpdateImpact, ...]) -> bytes:
    value = [impact.to_data() for impact in impacts]
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def _from_output_contract_diagnostic(
    value: contracts.Diagnostic,
) -> PackageLockUpdateDiagnostic:
    location = (
        f"{value.manifest_path}{value.pointer}"
        if value.pointer
        else value.manifest_path
    )
    return PackageLockUpdateDiagnostic(
        code=f"update.output.{value.code}",
        message=_redact(value.message),
        location=_redact(location),
    )


def _validate_resolution_output(
    lock: dict[str, Any],
    project: dict[str, Any],
    distribution: dict[str, Any],
    resolver_policy_version: int,
    selected_candidates: tuple[PackageCandidate, ...],
    captured_candidates: tuple[PackageCandidate, ...],
    validators: contracts.ContractValidators,
) -> tuple[PackageLockUpdateDiagnostic, ...]:
    diagnostics: list[PackageLockUpdateDiagnostic] = []
    for index, candidate in enumerate(selected_candidates):
        if (
            not isinstance(candidate.identity, str)
            or not isinstance(candidate.version, str)
            or not isinstance(candidate.package_kind, str)
            or not isinstance(candidate.origin, str)
            or not isinstance(candidate.source, dict)
            or not isinstance(candidate.manifest_integrity, dict)
            or not isinstance(candidate.payload_integrity, dict)
            or not isinstance(candidate.manifest, dict)
        ):
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.output.selected-candidate-metadata-invalid",
                    identity=_safe_identity(candidate.identity),
                    message="selected candidate metadata is not a canonical package candidate",
                    location=f"resolver/selectedCandidates/{index}",
                )
            )
    if diagnostics:
        return _ordered_diagnostics(diagnostics)

    expected_resolver = {
        "version": package_resolver.RESOLVER_VERSION,
        "policyVersion": resolver_policy_version,
    }
    if lock["resolver"] != expected_resolver:
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.output.resolver-policy-mismatch",
                message="proposed Lock does not record the resolver version and policy used",
                location="proposed/asharia.packages.lock.json/resolver",
            )
        )
    if lock["inputs"]["engine"] != _current_engine_input(distribution):
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.output.engine-input-mismatch",
                message="proposed Lock does not bind the exact current Distribution Engine input",
                location="proposed/asharia.packages.lock.json/inputs/engine",
            )
        )
    diagnostics.extend(
        _from_output_contract_diagnostic(item)
        for item in contracts.validate_locked_result_data(
            lock,
            project,
            [candidate.manifest for candidate in selected_candidates],
            validators,
            lock_path="proposed/asharia.packages.lock.json",
            project_path="proposed/asharia.packages.json",
        )
    )

    captured_multiset = Counter(
        _candidate_projection_key(candidate) for candidate in captured_candidates
    )
    for candidate in selected_candidates:
        key = _candidate_projection_key(candidate)
        if captured_multiset[key] <= 0:
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.output.selected-candidate-unbound",
                    identity=_safe_identity(candidate.identity),
                    message="selected candidate is not a member of the captured candidate multiset",
                    location="resolver/selectedCandidates",
                )
            )
        else:
            captured_multiset[key] -= 1

    selected_counts = Counter(candidate.identity for candidate in selected_candidates)
    selected_identities = tuple(candidate.identity for candidate in selected_candidates)
    if selected_identities != tuple(sorted(selected_identities, key=_utf8_key)):
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.output.selected-candidates-noncanonical",
                message="selected candidates must be sorted by UTF-8 package identity",
                location="resolver/selectedCandidates",
            )
        )
    nodes = {node["id"]: node for node in lock["nodes"]}
    for identity in sorted(
        set(selected_counts) | set(nodes),
        key=lambda value: _utf8_key(str(value)),
    ):
        count = selected_counts[identity]
        if count != 1:
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.output.selected-identity-count",
                    identity=_safe_identity(identity),
                    message="every proposed Lock node must have exactly one selected candidate",
                    location="resolver/selectedCandidates",
                )
            )
            continue
        if identity not in nodes:
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.output.selected-node-missing",
                    identity=_safe_identity(identity),
                    message="selected candidate has no proposed Lock node",
                    location="proposed/asharia.packages.lock.json/nodes",
                )
            )

    for candidate in selected_candidates:
        node = nodes.get(candidate.identity)
        if node is not None and not _candidate_matches_locked_node(candidate, node):
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.output.lock-node-unbound",
                    identity=_safe_identity(candidate.identity),
                    message="proposed Lock node does not bind the selected candidate exactly",
                    location="proposed/asharia.packages.lock.json/nodes",
                )
            )
    return _ordered_diagnostics(diagnostics)


def plan_package_lock_update(
    base_project: Any,
    proposed_project: Any,
    existing_lock: Any,
    distribution: Any,
    candidates: Iterable[PackageCandidate],
    validators: contracts.ContractValidators,
    request: LockUpdateRequest,
) -> PackageLockUpdatePlanResult:
    """Resolve and diff one explicit Lock update without IO or caller mutation."""

    normalized_request, request_diagnostics = _validate_request(request)
    if request_diagnostics:
        return _failure(request_diagnostics)
    assert normalized_request is not None

    diagnostics: list[PackageLockUpdateDiagnostic] = []
    try:
        captured_base_project = copy.deepcopy(base_project)
        captured_proposed_project = copy.deepcopy(proposed_project)
        captured_existing_lock = copy.deepcopy(existing_lock)
        captured_distribution = copy.deepcopy(distribution)
    except Exception:
        return _failure(
            (
                PackageLockUpdateDiagnostic(
                    code="update.input.capture-failed",
                    message="planner inputs could not be detached from caller-owned state",
                    location="inputs",
                ),
            )
        )
    try:
        raw_candidate_snapshot = tuple(candidates)
    except Exception:
        return _failure(
            (
                PackageLockUpdateDiagnostic(
                    code="update.input.candidates-invalid",
                    message="package candidates must be a finite iterable",
                    location="candidates",
                ),
            )
        )
    invalid_candidate_indices = tuple(
        index
        for index, candidate in enumerate(raw_candidate_snapshot)
        if not isinstance(candidate, PackageCandidate)
    )
    for index in invalid_candidate_indices:
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.input.candidates-invalid",
                message="every candidate must use the shared PackageCandidate contract",
                location=f"candidates/{index}",
            )
        )
    if diagnostics:
        return _failure(diagnostics)
    try:
        candidate_snapshot = tuple(
            _capture_candidate(candidate) for candidate in raw_candidate_snapshot
        )
        candidate_set_bytes = _candidate_set_bytes(candidate_snapshot)
    except Exception:
        return _failure(
            (
                PackageLockUpdateDiagnostic(
                    code="update.input.candidate-snapshot-invalid",
                    message="candidate snapshot contains non-canonical logical data",
                    location="candidates",
                ),
            )
        )

    for value, path in (
        (captured_base_project, "base/asharia.packages.json"),
        (captured_proposed_project, "proposed/asharia.packages.json"),
        (captured_existing_lock, "base/asharia.packages.lock.json"),
        (captured_distribution, contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME),
    ):
        diagnostics.extend(
            _from_contract_diagnostic(item)
            for item in contracts.validate_manifest_data(value, path, validators)
        )
    if diagnostics:
        return _failure(diagnostics)

    assert isinstance(captured_base_project, dict)
    assert isinstance(captured_proposed_project, dict)
    assert isinstance(captured_existing_lock, dict)
    assert isinstance(captured_distribution, dict)
    normalized_base_project = contracts.normalize_project_manifest(captured_base_project)
    normalized_proposed_project = contracts.normalize_project_manifest(
        captured_proposed_project
    )
    normalized_base_lock = contracts.normalize_lock_manifest(captured_existing_lock)
    normalized_distribution = contracts.normalize_engine_distribution_manifest(
        captured_distribution
    )

    expected_base_integrity = contracts.compute_project_manifest_integrity(
        normalized_base_project
    )
    if normalized_base_lock["inputs"]["projectManifestIntegrity"] != expected_base_integrity:
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.base.project-manifest-stale",
                message="base Lock does not match the normalized base Project Manifest",
                location="base/asharia.packages.lock.json/inputs/projectManifestIntegrity",
            )
        )
    base_engine = normalized_base_lock["inputs"]["engine"]
    base_project_engine = normalized_base_project["engine"]
    if base_engine["distributionId"] != base_project_engine["distributionId"]:
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.base.engine-distribution-mismatch",
                message="base Lock Distribution does not match the base Project Manifest",
                location="base/asharia.packages.lock.json/inputs/engine/distributionId",
            )
        )
    if not contracts.version_satisfies_constraint(
        base_engine["engineApiVersion"], base_project_engine["apiVersion"]
    ):
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.base.engine-api-incompatible",
                message="base Lock Engine API does not satisfy the base Project Manifest",
                location="base/asharia.packages.lock.json/inputs/engine/engineApiVersion",
            )
        )

    base_direct = _direct_requirements_by_identity(normalized_base_project)
    proposed_direct = _direct_requirements_by_identity(normalized_proposed_project)
    base_options = _package_options_by_identity(normalized_base_project)
    proposed_options = _package_options_by_identity(normalized_proposed_project)
    direct_changed = _changed_mapping_identities(base_direct, proposed_direct)
    option_changed = _changed_mapping_identities(base_options, proposed_options)
    engine_requirement_changed = (
        normalized_base_project["engine"] != normalized_proposed_project["engine"]
    )
    if (
        normalized_request.mode == TARGETED_CONSERVATIVE_MODE
        and engine_requirement_changed
    ):
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.request.engine-change-requires-full",
                message="Engine requirement changes require full update planning",
                location="proposed/asharia.packages.json/engine",
            )
        )
    current_engine_input = _current_engine_input(normalized_distribution)
    if (
        normalized_request.mode == TARGETED_CONSERVATIVE_MODE
        and normalized_base_lock["inputs"]["engine"] != current_engine_input
    ):
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.request.engine-input-change-requires-full",
                message=(
                    "current Distribution Engine API or generation differs from the base "
                    "Lock; use full update planning"
                ),
                location=f"{contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME}/engineGenerationId",
            )
        )
    if normalized_request.mode == TARGETED_CONSERVATIVE_MODE:
        for identity in sorted(
            set(normalized_request.intent_only_targets) - set(option_changed),
            key=_utf8_key,
        ):
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.request.intent-only-target-unused",
                    identity=identity,
                    message="intent-only target must authorize an actual package-options change",
                    location="request/intentOnlyTargets",
                )
            )
        for identity in sorted(
            set(direct_changed) - set(normalized_request.unlock_targets),
            key=_utf8_key,
        ):
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.request.direct-intent-change-untargeted",
                    identity=identity,
                    message="changed direct requirement must be explicitly unlocked",
                    location="request/unlockTargets",
                )
            )
        authorized_intent = set(normalized_request.unlock_targets) | set(
            normalized_request.intent_only_targets
        )
        for identity in sorted(
            set(option_changed) - authorized_intent,
            key=_utf8_key,
        ):
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.request.option-change-untargeted",
                    identity=identity,
                    message="changed package options require an explicit intent target",
                    location="request/intentOnlyTargets",
                )
            )

    base_nodes = {node["id"]: node for node in normalized_base_lock["nodes"]}
    if normalized_request.mode == TARGETED_CONSERVATIVE_MODE:
        for identity in normalized_request.intent_only_targets:
            if identity not in base_nodes:
                diagnostics.append(
                    PackageLockUpdateDiagnostic(
                        code="update.request.intent-only-baseline-missing",
                        identity=identity,
                        message="intent-only target must name an existing locked node",
                        location="request/intentOnlyTargets",
                    )
                )
    known_targets = (
        set(base_nodes)
        | set(base_direct)
        | set(proposed_direct)
        | set(base_options)
        | set(proposed_options)
    )
    all_targets = (
        normalized_request.unlock_targets + normalized_request.intent_only_targets
    )
    for identity in all_targets:
        if identity not in known_targets:
            diagnostics.append(
                PackageLockUpdateDiagnostic(
                    code="update.request.target-unknown",
                    identity=identity,
                    message="target is neither locked nor present in Project intent",
                    location="request",
                )
            )
    if diagnostics:
        return _failure(diagnostics)

    candidate_preferences: tuple[package_resolver.CandidatePreference, ...] = ()
    if normalized_request.mode == TARGETED_CONSERVATIVE_MODE:
        candidate_preferences = tuple(
            _preference_from_node(node)
            for identity, node in sorted(
                base_nodes.items(), key=lambda item: _utf8_key(item[0])
            )
            if identity not in normalized_request.unlock_targets
        )

    resolver_policy_version = (
        package_resolver.RESOLUTION_POLICY_VERSION
        if normalized_request.mode == FULL_UPDATE_MODE
        else package_resolver.LOCKED_PREFERENCE_POLICY_VERSION
    )
    try:
        resolver_project = copy.deepcopy(normalized_proposed_project)
        resolver_distribution = copy.deepcopy(normalized_distribution)
        resolver_candidates = tuple(
            _capture_candidate(candidate, resolver_origin=True)
            for candidate in candidate_snapshot
        )
        resolver_preferences = copy.deepcopy(candidate_preferences)
        resolution = package_resolver.resolve_package_graph(
            resolver_project,
            resolver_distribution,
            resolver_candidates,
            validators,
            policy_version=resolver_policy_version,
            candidate_preferences=resolver_preferences,
        )
    except Exception:
        return _failure(
            (
                PackageLockUpdateDiagnostic(
                    code="update.internal.resolver-failed",
                    message="resolver failed before producing an atomic result",
                    location="resolver",
                ),
            )
        )
    if not isinstance(resolution, package_resolver.ResolutionResult):
        return _failure(
            (
                PackageLockUpdateDiagnostic(
                    code="update.output.resolution-envelope-invalid",
                    message="resolver did not return the typed atomic result envelope",
                    location="resolver",
                ),
            )
        )
    if (
        type(resolution.diagnostics) is not tuple
        or type(resolution.selected_candidates) is not tuple
        or any(
            not isinstance(item, package_resolver.ResolverDiagnostic)
            for item in resolution.diagnostics
        )
        or (
            resolution.lock is None
            and (not resolution.diagnostics or resolution.selected_candidates)
        )
        or (resolution.lock is not None and resolution.diagnostics)
    ):
        return _failure(
            (
                PackageLockUpdateDiagnostic(
                    code="update.output.resolution-envelope-invalid",
                    message="resolver result violates the atomic success/failure envelope",
                    location="resolver",
                ),
            )
        )
    if resolution.lock is None:
        return _failure(
            _from_resolver_diagnostic(item) for item in resolution.diagnostics
        )
    output_manifest_diagnostics = contracts.validate_manifest_data(
        resolution.lock,
        "proposed/asharia.packages.lock.json",
        validators,
    )
    if output_manifest_diagnostics:
        return _failure(
            _from_output_contract_diagnostic(item)
            for item in output_manifest_diagnostics
        )
    normalized_proposed_lock = contracts.normalize_lock_manifest(resolution.lock)
    try:
        selected_candidates = tuple(
            _capture_candidate(candidate, portable_origin=True)
            for candidate in tuple(resolution.selected_candidates)
        )
        selected_candidate_set_bytes = _candidate_set_bytes(selected_candidates)
    except Exception:
        return _failure(
            (
                PackageLockUpdateDiagnostic(
                    code="update.output.selected-candidates-invalid",
                    message="resolver selected candidates are not a canonical immutable set",
                    location="resolver/selectedCandidates",
                ),
            )
        )
    output_diagnostics = _validate_resolution_output(
        normalized_proposed_lock,
        normalized_proposed_project,
        normalized_distribution,
        resolver_policy_version,
        selected_candidates,
        candidate_snapshot,
        validators,
    )
    if output_diagnostics:
        return _failure(output_diagnostics)

    for identity in _kind_changed_identities(
        normalized_base_lock,
        normalized_proposed_lock,
    ):
        diagnostics.append(
            PackageLockUpdateDiagnostic(
                code="update.impact.kind-changed",
                identity=identity,
                message="one package identity cannot change package kind",
                location="impacts",
            )
        )
    if diagnostics:
        return _failure(diagnostics)

    impacts = _derive_impacts(
        normalized_base_project,
        normalized_proposed_project,
        normalized_base_lock,
        normalized_proposed_lock,
        normalized_request,
    )

    base_project_bytes = _project_bytes(normalized_base_project)
    base_lock_bytes = _lock_bytes(normalized_base_lock)
    proposed_project_bytes = _project_bytes(normalized_proposed_project)
    resolved_lock_bytes = _lock_bytes(normalized_proposed_lock)
    project_manifest_changed = base_project_bytes != proposed_project_bytes
    engine_input_changed = (
        normalized_base_lock["inputs"]["engine"]
        != normalized_proposed_lock["inputs"]["engine"]
    )
    if not impacts and not project_manifest_changed and not engine_input_changed:
        normalized_proposed_lock = normalized_base_lock
        resolved_lock_bytes = base_lock_bytes
    status = (
        "no-changes"
        if base_project_bytes == proposed_project_bytes
        and base_lock_bytes == resolved_lock_bytes
        else "planned-changes"
    )
    impacts = _derive_impacts(
        normalized_base_project,
        normalized_proposed_project,
        normalized_base_lock,
        normalized_proposed_lock,
        normalized_request,
    )

    distribution_bytes = _distribution_bytes(normalized_distribution)
    request_bytes = _request_bytes(normalized_request, resolver_policy_version)
    impact_set_bytes = _impact_set_bytes(impacts)
    base_project_integrity = _domain_integrity(
        "com.asharia.package-lock-update/base-project/v1", base_project_bytes
    )
    base_lock_integrity = _domain_integrity(
        "com.asharia.package-lock-update/base-lock/v1", base_lock_bytes
    )
    proposed_project_integrity = _domain_integrity(
        "com.asharia.package-lock-update/proposed-project/v1",
        proposed_project_bytes,
    )
    distribution_integrity = _domain_integrity(
        "com.asharia.package-lock-update/distribution/v1", distribution_bytes
    )
    candidate_set_integrity = _domain_integrity(
        "com.asharia.package-lock-update/candidate-set/v1", candidate_set_bytes
    )
    request_integrity = _domain_integrity(
        "com.asharia.package-lock-update/request/v1", request_bytes
    )
    selected_candidate_set_integrity = _domain_integrity(
        "com.asharia.package-lock-update/selected-candidate-set/v1",
        selected_candidate_set_bytes,
    )
    proposed_lock_integrity = _domain_integrity(
        "com.asharia.package-lock-update/proposed-lock/v1", resolved_lock_bytes
    )
    impact_set_integrity = _domain_integrity(
        "com.asharia.package-lock-update/impact-set/v1", impact_set_bytes
    )
    engine_generation_id = normalized_distribution["engineGenerationId"]
    plan_payload = _plan_integrity_payload(
        mode=normalized_request.mode,
        resolver_policy_version=resolver_policy_version,
        unlock_targets=normalized_request.unlock_targets,
        intent_only_targets=normalized_request.intent_only_targets,
        status=status,
        project_manifest_changed=project_manifest_changed,
        engine_input_changed=engine_input_changed,
        impacts=impacts,
        base_project_integrity=base_project_integrity,
        base_lock_integrity=base_lock_integrity,
        proposed_project_integrity=proposed_project_integrity,
        distribution_integrity=distribution_integrity,
        candidate_set_integrity=candidate_set_integrity,
        request_integrity=request_integrity,
        selected_candidate_set_integrity=selected_candidate_set_integrity,
        proposed_lock_integrity=proposed_lock_integrity,
        impact_set_integrity=impact_set_integrity,
        engine_generation_id=engine_generation_id,
    )
    plan_integrity = _domain_integrity(
        "com.asharia.package-lock-update/plan/v1", plan_payload
    )

    plan = PackageLockUpdatePlan(
        mode=normalized_request.mode,
        policy_version=UPDATE_POLICY_VERSION,
        resolver_policy_version=resolver_policy_version,
        unlock_targets=normalized_request.unlock_targets,
        intent_only_targets=normalized_request.intent_only_targets,
        status=status,
        project_manifest_changed=project_manifest_changed,
        engine_input_changed=engine_input_changed,
        impacts=impacts,
        base_project_integrity=base_project_integrity,
        base_lock_integrity=base_lock_integrity,
        proposed_project_integrity=proposed_project_integrity,
        distribution_integrity=distribution_integrity,
        candidate_set_integrity=candidate_set_integrity,
        request_integrity=request_integrity,
        selected_candidate_set_integrity=selected_candidate_set_integrity,
        proposed_lock_integrity=proposed_lock_integrity,
        impact_set_integrity=impact_set_integrity,
        plan_integrity=plan_integrity,
        engine_generation_id=engine_generation_id,
        _base_project_bytes=base_project_bytes,
        _base_lock_bytes=base_lock_bytes,
        _proposed_project_bytes=proposed_project_bytes,
        _proposed_lock_bytes=resolved_lock_bytes,
        _selected_candidates=selected_candidates,
    )
    return PackageLockUpdatePlanResult(plan=plan, diagnostics=())
