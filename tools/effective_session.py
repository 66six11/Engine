"""Derived Effective Session v1 planning for one verified Engine/Project Host."""

from __future__ import annotations

import copy
import json
import re
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools.package_candidates import PackageCandidate
from tools.package_lock_verification import verify_locked_package_graph


EFFECTIVE_SESSION_PLAN_NAME = "asharia.effective-session.json"
EFFECTIVE_SESSION_PLAN_SCHEMA = "com.asharia.effective-session"
EFFECTIVE_SESSION_PLAN_SCHEMA_VERSION = 1


class EffectiveSessionState(str, Enum):
    """Stable session vocabulary; build/restart states are reserved in v1."""

    READY = "Ready"
    PENDING_BUILD = "PendingBuild"
    PENDING_RESTART = "PendingRestart"
    REPAIR_REQUIRED = "RepairRequired"
    UPGRADE_REQUIRED = "UpgradeRequired"
    SAFE_MODE = "SafeMode"


@dataclass(frozen=True, order=True)
class SessionIntegrity:
    """One structured SHA-256 fingerprint stored without mutable dictionaries."""

    algorithm: str
    digest: str


@dataclass(frozen=True, order=True)
class EffectiveSessionDiagnostic:
    """Stable diagnostic emitted by the session boundary."""

    code: str
    manifest_path: str
    pointer: str
    message: str

    def render(self) -> str:
        location = f"{self.manifest_path}{self.pointer}"
        return f"{location}: [{self.code}] {self.message}"


@dataclass(frozen=True)
class HostProfileSnapshot:
    """Distribution-relative Host Profile path, parsed data, and exact file bytes."""

    path: str
    manifest: Any
    exact_bytes: bytes


@dataclass(frozen=True)
class VerifiedResolvedGraph:
    """Deep-copied normalized graph and fingerprints from locked verification."""

    distribution: dict[str, Any]
    project: dict[str, Any]
    lock: dict[str, Any]
    selected_candidates: tuple[PackageCandidate, ...]
    engine_generation_id: str
    distribution_manifest_integrity: SessionIntegrity
    project_manifest_integrity: SessionIntegrity
    locked_graph_integrity: SessionIntegrity
    candidate_bindings_integrity: SessionIntegrity


@dataclass(frozen=True)
class EffectiveSessionPlan:
    """One Ready, derived, non-persistent session plan."""

    verified_graph: VerifiedResolvedGraph
    host_profile_path: str
    host_profile: dict[str, Any]
    host_profile_bytes: bytes
    host_profile_integrity: SessionIntegrity
    session_fingerprint: SessionIntegrity

    @property
    def host_kind(self) -> str:
        return self.host_profile["hostKind"]

    @property
    def target_platform(self) -> str:
        return self.host_profile["targetPlatform"]


@dataclass(frozen=True)
class EffectiveSessionResult:
    """Atomic result: one Ready plan or one actionable failure state."""

    state: EffectiveSessionState
    plan: EffectiveSessionPlan | None
    diagnostics: tuple[EffectiveSessionDiagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return (
            self.state is EffectiveSessionState.READY
            and self.plan is not None
            and not self.diagnostics
        )


_UPGRADE_CODES = {
    "lock.engine.distribution-mismatch",
    "lock.engine.api-incompatible",
    "lock.input.engine-distribution-stale",
    "lock.input.engine-api-stale",
    "lock.input.engine-generation-stale",
    "lock.input.project-distribution-mismatch",
    "lock.input.project-engine-api-mismatch",
}
_DISTRIBUTION_REPAIR_CODES = {
    "lock.engine.distribution-required",
    "lock.engine.package-not-distributed",
    "lock.engine.distribution-candidate-mismatch",
}
_NODE_POINTER = re.compile(r"^/nodes/(?P<index>[0-9]+)(?:/|$)")


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8")


def _diagnostic_sort_key(
    diagnostic: contracts.Diagnostic | EffectiveSessionDiagnostic,
) -> tuple[str, str, str, str]:
    return (
        diagnostic.manifest_path,
        diagnostic.pointer,
        diagnostic.code,
        diagnostic.message,
    )


def _session_diagnostic(
    code: str,
    pointer: str,
    message: str,
    *,
    manifest_path: str = EFFECTIVE_SESSION_PLAN_NAME,
) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code=code,
        manifest_path=manifest_path,
        pointer=pointer,
        message=message,
    )


def _public_diagnostic(
    diagnostic: contracts.Diagnostic,
) -> EffectiveSessionDiagnostic:
    return EffectiveSessionDiagnostic(
        code=diagnostic.code,
        manifest_path=diagnostic.manifest_path,
        pointer=diagnostic.pointer,
        message=diagnostic.message,
    )


def _deduplicate_diagnostics(
    diagnostics: Iterable[contracts.Diagnostic],
) -> tuple[contracts.Diagnostic, ...]:
    values = {
        (
            diagnostic.manifest_path,
            diagnostic.pointer,
            diagnostic.code,
            diagnostic.message,
        ): diagnostic
        for diagnostic in diagnostics
    }
    return tuple(sorted(values.values(), key=_diagnostic_sort_key))


def _integrity_record(value: dict[str, str]) -> SessionIntegrity:
    return SessionIntegrity(value["algorithm"], value["digest"])


def _integrity_data(value: SessionIntegrity) -> dict[str, str]:
    return {"algorithm": value.algorithm, "digest": value.digest}


def _integrity_for_bytes(data: bytes) -> SessionIntegrity:
    return _integrity_record(contracts.compute_bytes_integrity(data))


def _canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _optional_bytes_integrity(value: bytes | None) -> dict[str, str] | None:
    return contracts.compute_bytes_integrity(value) if value is not None else None


def _candidate_binding_data(candidate: PackageCandidate) -> dict[str, Any]:
    location = candidate.payload_location
    if isinstance(location, Path):
        location_value: str | None = location.as_posix()
    elif location is None:
        location_value = None
    else:
        location_value = str(location)
    return {
        "id": candidate.identity,
        "version": candidate.version,
        "packageKind": candidate.package_kind,
        "origin": candidate.origin,
        "source": candidate.source,
        "manifestIntegrity": candidate.manifest_integrity,
        "payloadIntegrity": candidate.payload_integrity,
        "manifest": candidate.manifest,
        "buildDescriptor": candidate.build_descriptor,
        "buildDescriptorIntegrity": candidate.build_descriptor_integrity,
        "buildDescriptorBytesIntegrity": _optional_bytes_integrity(
            candidate.build_descriptor_bytes
        ),
        "productDeclaration": candidate.product_declaration,
        "productDeclarationIntegrity": candidate.product_declaration_integrity,
        "productDeclarationBytesIntegrity": _optional_bytes_integrity(
            candidate.product_declaration_bytes
        ),
        "factoryDeclaration": candidate.factory_declaration,
        "factoryDeclarationIntegrity": candidate.factory_declaration_integrity,
        "factoryDeclarationBytesIntegrity": _optional_bytes_integrity(
            candidate.factory_declaration_bytes
        ),
        "payloadLocation": location_value,
    }


def _candidate_bindings_integrity(
    candidates: Iterable[PackageCandidate],
) -> SessionIntegrity:
    ordered = sorted(
        (_candidate_binding_data(candidate) for candidate in candidates),
        key=lambda value: (
            _utf8_key(value["id"]),
            _utf8_key(value["version"]),
            _utf8_key(value["packageKind"]),
        ),
    )
    return _integrity_for_bytes(_canonical_bytes(ordered))


def _graph_integrities(
    distribution: dict[str, Any],
    project: dict[str, Any],
    lock: dict[str, Any],
    candidates: tuple[PackageCandidate, ...],
) -> tuple[SessionIntegrity, SessionIntegrity, SessionIntegrity, SessionIntegrity]:
    distribution_bytes = contracts.render_normalized_engine_distribution_manifest(
        distribution
    ).encode("utf-8")
    project_bytes = contracts.render_normalized_project_manifest(project).encode("utf-8")
    lock_bytes = contracts.render_normalized_lock_manifest(lock).encode("utf-8")
    return (
        _integrity_for_bytes(distribution_bytes),
        _integrity_for_bytes(project_bytes),
        _integrity_for_bytes(lock_bytes),
        _candidate_bindings_integrity(candidates),
    )


def _session_payload_data(
    graph: VerifiedResolvedGraph,
    profile_path: str,
    profile: dict[str, Any],
    profile_integrity: SessionIntegrity,
) -> dict[str, Any]:
    distribution = graph.distribution["distribution"]
    return {
        "schema": EFFECTIVE_SESSION_PLAN_SCHEMA,
        "schemaVersion": EFFECTIVE_SESSION_PLAN_SCHEMA_VERSION,
        "state": EffectiveSessionState.READY.value,
        "inputs": {
            "engineDistribution": {
                "distributionId": distribution["id"],
                "engineApiVersion": distribution["engineApiVersion"],
                "engineGenerationId": graph.engine_generation_id,
                "manifestIntegrity": _integrity_data(
                    graph.distribution_manifest_integrity
                ),
            },
            "projectManifestIntegrity": _integrity_data(
                graph.project_manifest_integrity
            ),
            "lockedGraphIntegrity": _integrity_data(graph.locked_graph_integrity),
            "candidateBindingsIntegrity": _integrity_data(
                graph.candidate_bindings_integrity
            ),
            "hostProfile": {
                "path": profile_path,
                "hostKind": profile["hostKind"],
                "targetPlatform": profile["targetPlatform"],
                "integrity": _integrity_data(profile_integrity),
            },
        },
    }


def _session_fingerprint(
    graph: VerifiedResolvedGraph,
    profile_path: str,
    profile: dict[str, Any],
    profile_integrity: SessionIntegrity,
) -> SessionIntegrity:
    return _integrity_for_bytes(
        _canonical_bytes(
            _session_payload_data(graph, profile_path, profile, profile_integrity)
        )
    )


def effective_session_plan_to_data(plan: EffectiveSessionPlan) -> dict[str, Any]:
    """Return canonical diagnostic data; this is derived state, not a lockfile."""

    return {
        **_session_payload_data(
            plan.verified_graph,
            plan.host_profile_path,
            plan.host_profile,
            plan.host_profile_integrity,
        ),
        "sessionFingerprint": _integrity_data(plan.session_fingerprint),
    }


def render_effective_session_plan(plan: EffectiveSessionPlan) -> str:
    """Render byte-stable diagnostic JSON with LF and a final newline."""

    return json.dumps(
        effective_session_plan_to_data(plan),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def _verify_profile_snapshot(
    snapshot: Any,
    distribution: Any,
    validators: contracts.ContractValidators,
) -> tuple[dict[str, Any] | None, list[contracts.Diagnostic]]:
    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(snapshot, HostProfileSnapshot):
        return None, [
            _session_diagnostic(
                "session.profile.snapshot-invalid",
                "/hostProfile",
                "Host Profile input must be a HostProfileSnapshot",
            )
        ]
    if not isinstance(snapshot.path, str) or not snapshot.path:
        diagnostics.append(
            _session_diagnostic(
                "session.profile.path-invalid",
                "/hostProfile/path",
                "Host Profile snapshot path must be a non-empty relative path",
            )
        )
    if not isinstance(snapshot.exact_bytes, bytes):
        diagnostics.append(
            _session_diagnostic(
                "session.profile.bytes-invalid",
                "/hostProfile/bytes",
                "Host Profile snapshot must carry immutable exact bytes",
            )
        )
        parsed: Any = None
    else:
        try:
            parsed = json.loads(snapshot.exact_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            parsed = None
            diagnostics.append(
                _session_diagnostic(
                    "session.profile.bytes-invalid",
                    "/hostProfile/bytes",
                    "Host Profile bytes must be valid UTF-8 JSON without a BOM",
                )
            )
    if parsed is not None and not isinstance(parsed, dict):
        diagnostics.append(
            _session_diagnostic(
                "session.profile.document-invalid",
                "/hostProfile/bytes",
                "Host Profile bytes must contain one JSON object",
            )
        )
    if parsed is not None and parsed != snapshot.manifest:
        diagnostics.append(
            _session_diagnostic(
                "session.profile.snapshot-mismatch",
                "/hostProfile/manifest",
                "parsed Host Profile data does not match the captured exact bytes",
            )
        )

    profile = parsed if isinstance(parsed, dict) else snapshot.manifest
    if isinstance(profile, dict):
        diagnostics.extend(
            contracts.validate_manifest_data(
                profile,
                contracts.HOST_PROFILE_NAME,
                validators,
            )
        )
    else:
        diagnostics.append(
            _session_diagnostic(
                "session.profile.document-invalid",
                "/hostProfile/manifest",
                "Host Profile snapshot manifest must be an object",
            )
        )

    distribution_diagnostics = contracts.validate_manifest_data(
        distribution,
        contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
        validators,
    )
    diagnostics.extend(distribution_diagnostics)
    if diagnostics:
        return None, diagnostics

    assert isinstance(profile, dict)
    assert isinstance(distribution, dict)
    normalized_profile = contracts.normalize_host_profile(profile)
    normalized_distribution = contracts.normalize_engine_distribution_manifest(
        distribution
    )
    matches = [
        reference
        for reference in normalized_distribution["hostProfiles"]
        if reference["path"] == snapshot.path
        and reference["hostKind"] == normalized_profile["hostKind"]
        and reference["targetPlatform"] == normalized_profile["targetPlatform"]
    ]
    if not matches:
        diagnostics.append(
            _session_diagnostic(
                "session.profile.reference-missing",
                "/hostProfile",
                (
                    "Engine Distribution does not reference this Host Profile path, "
                    "kind, and platform"
                ),
            )
        )
    elif len(matches) > 1:
        diagnostics.append(
            _session_diagnostic(
                "session.profile.reference-ambiguous",
                "/hostProfile",
                "Engine Distribution references the Host Profile more than once",
            )
        )
    else:
        assert isinstance(snapshot.exact_bytes, bytes)
        actual_integrity = contracts.compute_bytes_integrity(snapshot.exact_bytes)
        if actual_integrity != matches[0]["integrity"]:
            diagnostics.append(
                _session_diagnostic(
                    "session.profile.integrity-mismatch",
                    "/hostProfile/bytes",
                    "Host Profile exact bytes do not match Engine Distribution inventory",
                )
            )
    if (
        normalized_profile["targetPlatform"]
        != normalized_distribution["context"]["targetPlatform"]
    ):
        diagnostics.append(
            _session_diagnostic(
                "session.profile.platform-mismatch",
                "/hostProfile/targetPlatform",
                "Host Profile platform does not match Engine Distribution context",
            )
        )
    return (
        normalized_profile if not diagnostics else None,
        diagnostics,
    )


def _node_is_distribution_owned(diagnostic: contracts.Diagnostic, lock: Any) -> bool:
    if not isinstance(lock, dict):
        return False
    nodes = lock.get("nodes")
    if not isinstance(nodes, list):
        return False
    match = _NODE_POINTER.match(diagnostic.pointer)
    if match is not None:
        index = int(match.group("index"))
        if 0 <= index < len(nodes):
            node = nodes[index]
            return (
                isinstance(node, dict)
                and isinstance(node.get("source"), dict)
                and node["source"].get("kind") == "engine-distribution"
            )
    candidate_prefix = "candidate/"
    if diagnostic.manifest_path.startswith(candidate_prefix):
        identity = diagnostic.manifest_path[len(candidate_prefix) :].split("/", 1)[0]
        return any(
            isinstance(node, dict)
            and node.get("id") == identity
            and isinstance(node.get("source"), dict)
            and node["source"].get("kind") == "engine-distribution"
            for node in nodes
        )
    return False


def _classify_failure_state(
    diagnostics: Iterable[contracts.Diagnostic],
    existing_lock: Any,
) -> EffectiveSessionState:
    repair = False
    upgrade = False
    for diagnostic in diagnostics:
        if diagnostic.code == "lock.engine.distribution-shadowed":
            continue
        if (
            diagnostic.manifest_path == contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME
            or diagnostic.manifest_path == contracts.HOST_PROFILE_NAME
            or diagnostic.code.startswith("distribution.")
            or diagnostic.code.startswith("session.profile.")
            or diagnostic.code in _DISTRIBUTION_REPAIR_CODES
            or _node_is_distribution_owned(diagnostic, existing_lock)
        ):
            repair = True
        elif diagnostic.code in _UPGRADE_CODES:
            upgrade = True
    if repair:
        return EffectiveSessionState.REPAIR_REQUIRED
    if upgrade:
        return EffectiveSessionState.UPGRADE_REQUIRED
    return EffectiveSessionState.SAFE_MODE


def _candidate_binding_diagnostics(
    graph: VerifiedResolvedGraph,
) -> list[contracts.Diagnostic]:
    diagnostics: list[contracts.Diagnostic] = []
    nodes = graph.lock["nodes"]
    inventory = {
        package["id"]: package for package in graph.distribution["bundledPackages"]
    }
    candidates_by_id: dict[str, list[PackageCandidate]] = {}
    for candidate in graph.selected_candidates:
        if not isinstance(candidate, PackageCandidate):
            diagnostics.append(
                _session_diagnostic(
                    "session.snapshot.candidate-invalid",
                    "/verifiedResolvedGraph/candidates",
                    "every session candidate must use PackageCandidate",
                )
            )
            continue
        candidates_by_id.setdefault(candidate.identity, []).append(candidate)

    node_ids = {node["id"] for node in nodes}
    for identity in sorted(set(candidates_by_id) - node_ids, key=_utf8_key):
        diagnostics.append(
            _session_diagnostic(
                "session.snapshot.candidate-extra",
                "/verifiedResolvedGraph/candidates",
                f"session contains candidate '{identity}' outside the locked graph",
            )
        )

    for index, node in enumerate(nodes):
        identity = node["id"]
        matching = candidates_by_id.get(identity, [])
        if len(matching) != 1:
            diagnostics.append(
                _session_diagnostic(
                    "session.snapshot.candidate-binding",
                    f"/verifiedResolvedGraph/lock/nodes/{index}",
                    f"locked package '{identity}' must have exactly one session candidate",
                )
            )
            continue
        candidate = matching[0]
        if (
            candidate.version != node["version"]
            or candidate.package_kind != node["packageKind"]
            or candidate.source != node["source"]
        ):
            diagnostics.append(
                _session_diagnostic(
                    "session.snapshot.candidate-binding",
                    f"/verifiedResolvedGraph/lock/nodes/{index}",
                    f"session candidate metadata changed for '{identity}'",
                )
            )
            continue
        if node["source"]["kind"] == "engine-distribution":
            package = inventory.get(identity)
            if package is None or (
                candidate.origin != f"engine-distribution:{package['root']}"
                or candidate.manifest_integrity != package["manifestIntegrity"]
                or candidate.payload_integrity != package["payloadIntegrity"]
            ):
                diagnostics.append(
                    _session_diagnostic(
                        "session.snapshot.distribution-binding",
                        f"/verifiedResolvedGraph/lock/nodes/{index}",
                        f"distributed session candidate changed for '{identity}'",
                    )
                )
        elif (
            candidate.manifest_integrity != node["manifestIntegrity"]
            or candidate.payload_integrity != node["payloadIntegrity"]
        ):
            diagnostics.append(
                _session_diagnostic(
                    "session.snapshot.candidate-binding",
                    f"/verifiedResolvedGraph/lock/nodes/{index}",
                    f"project-owned session evidence changed for '{identity}'",
                )
            )
    return diagnostics


def validate_verified_resolved_graph(
    graph: Any,
    validators: contracts.ContractValidators,
) -> tuple[EffectiveSessionDiagnostic, ...]:
    """Validate normalized graph bindings and stored fingerprints without disk IO."""

    if not isinstance(graph, VerifiedResolvedGraph):
        return (
            EffectiveSessionDiagnostic(
                code="session.input.verified-graph-required",
                manifest_path=EFFECTIVE_SESSION_PLAN_NAME,
                pointer="/verifiedResolvedGraph",
                message="consumer requires a VerifiedResolvedGraph",
            ),
        )
    diagnostics: list[contracts.Diagnostic] = []
    for manifest, path in (
        (graph.distribution, contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME),
        (graph.project, contracts.PROJECT_MANIFEST_NAME),
        (graph.lock, contracts.PACKAGE_LOCK_NAME),
    ):
        diagnostics.extend(
            contracts.validate_manifest_data(manifest, path, validators)
        )
    if diagnostics:
        return tuple(
            _public_diagnostic(value)
            for value in _deduplicate_diagnostics(diagnostics)
        )

    if not isinstance(graph.selected_candidates, tuple):
        return (
            EffectiveSessionDiagnostic(
                code="session.snapshot.candidates-invalid",
                manifest_path=EFFECTIVE_SESSION_PLAN_NAME,
                pointer="/verifiedResolvedGraph/candidates",
                message="Verified graph candidates must be an immutable tuple snapshot",
            ),
        )
    candidates = graph.selected_candidates
    if any(not isinstance(candidate, PackageCandidate) for candidate in candidates):
        return (
            EffectiveSessionDiagnostic(
                code="session.snapshot.candidate-invalid",
                manifest_path=EFFECTIVE_SESSION_PLAN_NAME,
                pointer="/verifiedResolvedGraph/candidates",
                message="Verified graph candidates must use PackageCandidate records",
            ),
        )
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
    if candidates != ordered_candidates:
        diagnostics.append(
            _session_diagnostic(
                "session.snapshot.not-normalized",
                "/verifiedResolvedGraph/candidates",
                "Verified graph candidates are not in canonical identity order",
            )
        )

    normalized_distribution = contracts.normalize_engine_distribution_manifest(
        graph.distribution
    )
    normalized_project = contracts.normalize_project_manifest(graph.project)
    normalized_lock = contracts.normalize_lock_manifest(graph.lock)
    for current, normalized, pointer, label in (
        (
            graph.distribution,
            normalized_distribution,
            "/verifiedResolvedGraph/distribution",
            "Engine Distribution",
        ),
        (
            graph.project,
            normalized_project,
            "/verifiedResolvedGraph/project",
            "Project Manifest",
        ),
        (
            graph.lock,
            normalized_lock,
            "/verifiedResolvedGraph/lock",
            "Project Lock",
        ),
    ):
        if current != normalized:
            diagnostics.append(
                _session_diagnostic(
                    "session.snapshot.not-normalized",
                    pointer,
                    f"Verified graph {label} snapshot is not normalized",
                )
            )
    if graph.engine_generation_id != normalized_distribution["engineGenerationId"]:
        diagnostics.append(
            _session_diagnostic(
                "session.snapshot.engine-generation",
                "/verifiedResolvedGraph/engineGenerationId",
                "Verified graph generation no longer matches its Distribution",
            )
        )
    expected_engine = {
        "distributionId": normalized_distribution["distribution"]["id"],
        "engineApiVersion": normalized_distribution["distribution"][
            "engineApiVersion"
        ],
        "engineGenerationId": normalized_distribution["engineGenerationId"],
    }
    if normalized_lock["inputs"]["engine"] != expected_engine:
        diagnostics.append(
            _session_diagnostic(
                "session.snapshot.engine-input",
                "/verifiedResolvedGraph/lock/inputs/engine",
                "Verified graph Lock no longer matches its Engine Distribution",
            )
        )
    if normalized_lock["inputs"][
        "projectManifestIntegrity"
    ] != contracts.compute_project_manifest_integrity(normalized_project):
        diagnostics.append(
            _session_diagnostic(
                "session.snapshot.project-input",
                "/verifiedResolvedGraph/lock/inputs/projectManifestIntegrity",
                "Verified graph Lock no longer matches its Project Manifest",
            )
        )
    diagnostics.extend(_candidate_binding_diagnostics(graph))
    diagnostics.extend(
        contracts.validate_locked_result_data(
            normalized_lock,
            normalized_project,
            [candidate.manifest for candidate in candidates],
            validators,
        )
    )
    try:
        expected_integrities = _graph_integrities(
            normalized_distribution,
            normalized_project,
            normalized_lock,
            candidates,
        )
    except (AttributeError, KeyError, TypeError, ValueError):
        diagnostics.append(
            _session_diagnostic(
                "session.snapshot.candidate-invalid",
                "/verifiedResolvedGraph/candidates",
                "Verified graph candidate snapshot cannot be canonically fingerprinted",
            )
        )
        return tuple(
            _public_diagnostic(value)
            for value in _deduplicate_diagnostics(diagnostics)
        )
    actual_integrities = (
        graph.distribution_manifest_integrity,
        graph.project_manifest_integrity,
        graph.locked_graph_integrity,
        graph.candidate_bindings_integrity,
    )
    labels = ("distribution", "project", "lock", "candidate-bindings")
    for label, actual, expected in zip(labels, actual_integrities, expected_integrities):
        if actual != expected:
            diagnostics.append(
                _session_diagnostic(
                    "session.snapshot.fingerprint-mismatch",
                    f"/verifiedResolvedGraph/{label}",
                    f"Verified graph {label} fingerprint no longer matches its snapshot",
                )
            )
    return tuple(
        _public_diagnostic(value)
        for value in _deduplicate_diagnostics(diagnostics)
    )


def validate_ready_effective_session(
    plan: Any,
    validators: contracts.ContractValidators,
) -> tuple[EffectiveSessionDiagnostic, ...]:
    """Fail closed if a Ready session or any nested snapshot was forged or mutated."""

    if not isinstance(plan, EffectiveSessionPlan):
        return (
            EffectiveSessionDiagnostic(
                code="session.input.ready-plan-required",
                manifest_path=EFFECTIVE_SESSION_PLAN_NAME,
                pointer="",
                message="consumer requires an EffectiveSessionPlan produced in Ready state",
            ),
        )
    graph = plan.verified_graph
    if not isinstance(graph, VerifiedResolvedGraph):
        return (
            EffectiveSessionDiagnostic(
                code="session.input.verified-graph-required",
                manifest_path=EFFECTIVE_SESSION_PLAN_NAME,
                pointer="/verifiedResolvedGraph",
                message="Ready session must contain a VerifiedResolvedGraph",
            ),
        )

    graph_diagnostics = validate_verified_resolved_graph(graph, validators)
    normalized_profile, profile_diagnostics = _verify_profile_snapshot(
        HostProfileSnapshot(
            plan.host_profile_path,
            plan.host_profile,
            plan.host_profile_bytes,
        ),
        graph.distribution,
        validators,
    )
    if graph_diagnostics or profile_diagnostics:
        public_profile_diagnostics = tuple(
            _public_diagnostic(value)
            for value in _deduplicate_diagnostics(profile_diagnostics)
        )
        return tuple(
            sorted(
                (*graph_diagnostics, *public_profile_diagnostics),
                key=_diagnostic_sort_key,
            )
        )

    diagnostics: list[contracts.Diagnostic] = []
    assert normalized_profile is not None
    if plan.host_profile != normalized_profile:
        diagnostics.append(
            _session_diagnostic(
                "session.snapshot.not-normalized",
                "/hostProfile/manifest",
                "Ready session Host Profile snapshot is not normalized",
            )
        )
    expected_profile_integrity = _integrity_for_bytes(plan.host_profile_bytes)
    if plan.host_profile_integrity != expected_profile_integrity:
        diagnostics.append(
            _session_diagnostic(
                "session.snapshot.fingerprint-mismatch",
                "/hostProfile/integrity",
                "Ready session Host Profile fingerprint no longer matches exact bytes",
            )
        )
    expected_session_fingerprint = _session_fingerprint(
        graph,
        plan.host_profile_path,
        normalized_profile,
        expected_profile_integrity,
    )
    if plan.session_fingerprint != expected_session_fingerprint:
        diagnostics.append(
            _session_diagnostic(
                "session.snapshot.fingerprint-mismatch",
                "/sessionFingerprint",
                "Ready session fingerprint no longer matches its inputs",
            )
        )
    return tuple(
        _public_diagnostic(value)
        for value in _deduplicate_diagnostics(diagnostics)
    )


def plan_effective_session(
    distribution: Any,
    project: Any,
    existing_lock: Any,
    candidates: Iterable[PackageCandidate],
    host_profile_snapshot: Any,
    validators: contracts.ContractValidators,
) -> EffectiveSessionResult:
    """Verify and derive one session without resolving, writing, building, or loading."""

    verification = verify_locked_package_graph(
        project,
        distribution,
        existing_lock,
        candidates,
        validators,
    )
    normalized_profile, profile_diagnostics = _verify_profile_snapshot(
        host_profile_snapshot,
        distribution,
        validators,
    )
    diagnostics = _deduplicate_diagnostics(
        (*verification.diagnostics, *profile_diagnostics)
    )
    if diagnostics:
        return EffectiveSessionResult(
            state=_classify_failure_state(diagnostics, existing_lock),
            plan=None,
            diagnostics=tuple(_public_diagnostic(value) for value in diagnostics),
        )

    assert verification.lock is not None
    assert normalized_profile is not None
    assert isinstance(distribution, dict)
    assert isinstance(project, dict)
    assert isinstance(host_profile_snapshot, HostProfileSnapshot)
    normalized_distribution = contracts.normalize_engine_distribution_manifest(
        distribution
    )
    normalized_project = contracts.normalize_project_manifest(project)
    normalized_lock = contracts.normalize_lock_manifest(verification.lock)
    selected_candidates = tuple(
        sorted(
            copy.deepcopy(verification.selected_candidates),
            key=lambda candidate: (
                _utf8_key(candidate.identity),
                _utf8_key(candidate.version),
                _utf8_key(candidate.package_kind),
            ),
        )
    )
    (
        distribution_integrity,
        project_integrity,
        lock_integrity,
        candidate_integrity,
    ) = _graph_integrities(
        normalized_distribution,
        normalized_project,
        normalized_lock,
        selected_candidates,
    )
    graph = VerifiedResolvedGraph(
        distribution=copy.deepcopy(normalized_distribution),
        project=copy.deepcopy(normalized_project),
        lock=copy.deepcopy(normalized_lock),
        selected_candidates=selected_candidates,
        engine_generation_id=normalized_distribution["engineGenerationId"],
        distribution_manifest_integrity=distribution_integrity,
        project_manifest_integrity=project_integrity,
        locked_graph_integrity=lock_integrity,
        candidate_bindings_integrity=candidate_integrity,
    )
    profile_bytes = bytes(host_profile_snapshot.exact_bytes)
    profile_integrity = _integrity_for_bytes(profile_bytes)
    normalized_profile_snapshot = copy.deepcopy(normalized_profile)
    plan = EffectiveSessionPlan(
        verified_graph=graph,
        host_profile_path=host_profile_snapshot.path,
        host_profile=normalized_profile_snapshot,
        host_profile_bytes=profile_bytes,
        host_profile_integrity=profile_integrity,
        session_fingerprint=_session_fingerprint(
            graph,
            host_profile_snapshot.path,
            normalized_profile_snapshot,
            profile_integrity,
        ),
    )
    snapshot_diagnostics = validate_ready_effective_session(plan, validators)
    if snapshot_diagnostics:
        return EffectiveSessionResult(
            state=EffectiveSessionState.SAFE_MODE,
            plan=None,
            diagnostics=snapshot_diagnostics,
        )
    return EffectiveSessionResult(
        state=EffectiveSessionState.READY,
        plan=plan,
        diagnostics=(),
    )
