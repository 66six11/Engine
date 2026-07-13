"""Read-only verification and reuse of an existing Asharia package lock graph."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools.package_candidates import PackageCandidate


@dataclass(frozen=True)
class LockedGraphVerificationResult:
    """Atomic result: either one reusable exact graph or stable diagnostics."""

    lock: dict[str, Any] | None
    selected_candidates: tuple[PackageCandidate, ...]
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.lock is not None and not self.diagnostics


def _diagnostic_sort_key(
    diagnostic: contracts.Diagnostic,
) -> tuple[str, str, str, str]:
    return (
        diagnostic.manifest_path,
        diagnostic.pointer,
        diagnostic.code,
        diagnostic.message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> LockedGraphVerificationResult:
    return LockedGraphVerificationResult(
        lock=None,
        selected_candidates=(),
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _lock_diagnostic(
    code: str,
    pointer: str,
    message: str,
) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code=code,
        manifest_path=contracts.PACKAGE_LOCK_NAME,
        pointer=pointer,
        message=message,
    )


def _stable_json(value: Any) -> str | None:
    try:
        return json.dumps(value, sort_keys=True, separators=(",", ":"))
    except (TypeError, ValueError):
        return None


def _bind_locked_candidates(
    lock: dict[str, Any],
    candidates: tuple[PackageCandidate, ...],
    validators: contracts.ContractValidators,
) -> tuple[tuple[PackageCandidate, ...], list[contracts.Diagnostic]]:
    candidates_by_source: dict[str, list[PackageCandidate]] = {}
    for candidate in candidates:
        source_key = _stable_json(candidate.source)
        if source_key is not None:
            candidates_by_source.setdefault(source_key, []).append(candidate)

    selected: list[PackageCandidate] = []
    diagnostics: list[contracts.Diagnostic] = []
    for node_index, node in enumerate(lock["nodes"]):
        identity = node["id"]
        node_pointer = f"/nodes/{node_index}"
        source_key = _stable_json(node["source"])
        matching = candidates_by_source.get(source_key or "", [])
        if not matching:
            diagnostics.append(
                _lock_diagnostic(
                    "lock.source.unavailable",
                    f"{node_pointer}/source",
                    f"selected source for '{identity}' is unavailable",
                )
            )
            continue
        if len(matching) > 1:
            diagnostics.append(
                _lock_diagnostic(
                    "lock.candidate.ambiguous-binding",
                    f"{node_pointer}/source",
                    f"selected source for '{identity}' maps to multiple candidates",
                )
            )
            continue

        candidate = matching[0]
        diagnostic_count = len(diagnostics)
        for attribute, pointer, actual, expected, code in (
            (
                "identity",
                "id",
                candidate.identity,
                node["id"],
                "lock.candidate.identity-mismatch",
            ),
            (
                "version",
                "version",
                candidate.version,
                node["version"],
                "lock.candidate.version-mismatch",
            ),
            (
                "package kind",
                "packageKind",
                candidate.package_kind,
                node["packageKind"],
                "lock.candidate.kind-mismatch",
            ),
        ):
            if actual != expected:
                diagnostics.append(
                    _lock_diagnostic(
                        code,
                        f"{node_pointer}/{pointer}",
                        (
                            f"selected source for '{identity}' reports {attribute} "
                            f"'{actual}' instead of locked value '{expected}'"
                        ),
                    )
                )

        candidate_path = f"candidate/{identity}/{contracts.PACKAGE_MANIFEST_NAME}"
        manifest_diagnostics = contracts.validate_manifest_data(
            candidate.manifest,
            candidate_path,
            validators,
        )
        diagnostics.extend(manifest_diagnostics)
        if not manifest_diagnostics:
            for attribute, actual, expected in (
                ("identity", candidate.identity, candidate.manifest["id"]),
                ("version", candidate.version, candidate.manifest["version"]),
                ("packageKind", candidate.package_kind, candidate.manifest["packageKind"]),
            ):
                if actual != expected:
                    diagnostics.append(
                        _lock_diagnostic(
                            "lock.candidate.metadata-mismatch",
                            node_pointer,
                            (
                                f"candidate {attribute} '{actual}' does not match its author "
                                f"manifest value '{expected}' for '{identity}'"
                            ),
                        )
                    )

        if candidate.manifest_integrity != node["manifestIntegrity"]:
            diagnostics.append(
                _lock_diagnostic(
                    "lock.integrity.manifest-mismatch",
                    f"{node_pointer}/manifestIntegrity",
                    f"candidate manifest evidence changed for '{identity}'",
                )
            )
        if candidate.payload_integrity != node["payloadIntegrity"]:
            diagnostics.append(
                _lock_diagnostic(
                    "lock.integrity.payload-mismatch",
                    f"{node_pointer}/payloadIntegrity",
                    f"candidate payload evidence changed for '{identity}'",
                )
            )
        if not isinstance(candidate.origin, str) or not candidate.origin:
            diagnostics.append(
                _lock_diagnostic(
                    "lock.candidate.origin-invalid",
                    f"{node_pointer}/source",
                    f"candidate origin for '{identity}' must be a non-empty stable string",
                )
            )
        if not isinstance(candidate.payload_location, Path):
            diagnostics.append(
                _lock_diagnostic(
                    "lock.source.unavailable",
                    f"{node_pointer}/source",
                    f"selected source for '{identity}' has no verifiable payload location",
                )
            )

        if len(diagnostics) == diagnostic_count:
            selected.append(candidate)

    return tuple(selected), diagnostics


def verify_locked_package_graph(
    project: Any,
    engine_api_version: str,
    existing_lock: Any,
    candidates: Iterable[PackageCandidate],
    validators: contracts.ContractValidators,
) -> LockedGraphVerificationResult:
    """Prove that an existing exact lock graph is reusable without resolving or writing."""

    diagnostics: list[contracts.Diagnostic] = []
    diagnostics.extend(
        contracts.validate_manifest_data(
            project,
            contracts.PROJECT_MANIFEST_NAME,
            validators,
        )
    )
    if existing_lock is None:
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.missing",
                "",
                "an existing package lock is required for locked verification",
            )
        )
    else:
        diagnostics.extend(
            contracts.validate_manifest_data(
                existing_lock,
                contracts.PACKAGE_LOCK_NAME,
                validators,
            )
        )

    try:
        contracts.compare_semantic_versions(engine_api_version, engine_api_version)
    except (TypeError, ValueError):
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.engine-api-invalid",
                "/inputs/engineApiVersion",
                "current engine API version must be exact Semantic Version",
            )
        )

    try:
        candidate_snapshot = tuple(candidates)
    except TypeError:
        candidate_snapshot = ()
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.candidates-invalid",
                "",
                "package candidates must be iterable",
            )
        )
    if any(not isinstance(candidate, PackageCandidate) for candidate in candidate_snapshot):
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.candidates-invalid",
                "",
                "every package candidate must use the shared PackageCandidate contract",
            )
        )

    if diagnostics:
        return _failure(diagnostics)

    assert isinstance(existing_lock, dict)
    normalized_lock = contracts.normalize_lock_manifest(existing_lock)
    if normalized_lock["inputs"]["engineApiVersion"] != engine_api_version:
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.engine-api-stale",
                "/inputs/engineApiVersion",
                (
                    f"lock engine API '{normalized_lock['inputs']['engineApiVersion']}' "
                    f"does not match current engine API '{engine_api_version}'"
                ),
            )
        )
    if normalized_lock["inputs"][
        "projectManifestIntegrity"
    ] != contracts.compute_project_manifest_integrity(project):
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.project-manifest-stale",
                "/inputs/projectManifestIntegrity",
                "lock does not match the normalized Project Manifest",
            )
        )
    if diagnostics:
        return _failure(diagnostics)

    selected_candidates, binding_diagnostics = _bind_locked_candidates(
        normalized_lock,
        candidate_snapshot,
        validators,
    )
    if binding_diagnostics:
        return _failure(binding_diagnostics)

    cross_document_diagnostics = contracts.validate_locked_result_data(
        normalized_lock,
        project,
        [candidate.manifest for candidate in selected_candidates],
        validators,
    )
    if cross_document_diagnostics:
        return _failure(cross_document_diagnostics)

    payload_roots = {
        candidate.identity: candidate.payload_location for candidate in selected_candidates
    }
    integrity_diagnostics = contracts.validate_locked_candidate_integrity(
        normalized_lock,
        payload_roots,
    )
    if integrity_diagnostics:
        return _failure(integrity_diagnostics)

    return LockedGraphVerificationResult(
        lock=normalized_lock,
        selected_candidates=selected_candidates,
        diagnostics=(),
    )
