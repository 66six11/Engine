"""Read-only project package inspection for Bootstrap project-open sessions."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable

from tools import bootstrap_candidate_locations
from tools import bootstrap_session
from tools import check_package_contracts as contracts
from tools import effective_session
from tools import engine_distribution_repair_verifier as distribution_verifier


_CONTROL_PLANE_PATH = bootstrap_session.BOOTSTRAP_SESSION_REPORT_NAME


def _diagnostic_sort_key(
    value: bootstrap_session.BootstrapSessionDiagnostic,
) -> tuple[str, str, str, str]:
    return value.manifest_path, value.pointer, value.code, value.message


def _ordered_diagnostics(
    values: Iterable[bootstrap_session.BootstrapSessionDiagnostic],
) -> tuple[bootstrap_session.BootstrapSessionDiagnostic, ...]:
    unique = {
        (value.manifest_path, value.pointer, value.code, value.message): value
        for value in values
    }
    return tuple(sorted(unique.values(), key=_diagnostic_sort_key))


def _inspection(
    *,
    snapshot: bootstrap_session.ProjectPackageSnapshotV1 | None = None,
    session: effective_session.EffectiveSessionResult | None = None,
    owners: Iterable[bootstrap_session.InspectionFailureOwnerV1] = (),
    diagnostics: Iterable[bootstrap_session.BootstrapSessionDiagnostic] = (),
) -> bootstrap_session.ProjectOpenInspectionV1:
    return bootstrap_session.ProjectOpenInspectionV1(
        package_snapshot=snapshot,
        effective_session=session,
        failure_owners=tuple(sorted(set(owners), key=lambda value: value.value)),
        diagnostics=_ordered_diagnostics(diagnostics),
    )


def _public_contract_diagnostic(
    value: contracts.Diagnostic,
) -> bootstrap_session.BootstrapSessionDiagnostic:
    return bootstrap_session.BootstrapSessionDiagnostic(
        value.code,
        value.manifest_path,
        value.pointer,
        value.message,
    )


def _control_plane_diagnostic(
    code: str,
    pointer: str,
    message: str,
) -> bootstrap_session.BootstrapSessionDiagnostic:
    return bootstrap_session.BootstrapSessionDiagnostic(
        code,
        _CONTROL_PLANE_PATH,
        pointer,
        message,
    )


def _project_diagnostic(
    code: str,
    manifest_path: str,
    pointer: str,
    message: str,
) -> bootstrap_session.BootstrapSessionDiagnostic:
    return bootstrap_session.BootstrapSessionDiagnostic(
        code,
        manifest_path,
        pointer,
        message,
    )


def _is_link(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction is not None and is_junction())


def _canonical_project_root(
    value: object,
) -> tuple[Path | None, tuple[bootstrap_session.BootstrapSessionDiagnostic, ...]]:
    if not isinstance(value, Path):
        return None, (
            _control_plane_diagnostic(
                "bootstrap.inspection.project-root-invalid",
                "/request/projectRoot",
                "project root must be a pathlib.Path",
            ),
        )
    try:
        root = value.resolve(strict=True)
        if not root.is_dir():
            raise NotADirectoryError
    except (OSError, RuntimeError):
        return None, (
            _control_plane_diagnostic(
                "bootstrap.project.root-unavailable",
                "/request/projectRoot",
                "project root must resolve to an available directory",
            ),
        )
    return root, ()


def _read_exact_document(
    project_root: Path,
    name: str,
) -> tuple[
    dict[str, Any] | None,
    bytes | None,
    list[bootstrap_session.BootstrapSessionDiagnostic],
]:
    path = project_root / name
    try:
        if _is_link(path) or not path.is_file():
            raise OSError
        exact_bytes = path.read_bytes()
    except OSError:
        return None, None, [
            _project_diagnostic(
                "bootstrap.project.contract-unavailable",
                name,
                "",
                f"'{name}' must be an available regular file under the project root",
            )
        ]
    try:
        parsed = json.loads(exact_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None, exact_bytes, [
            _project_diagnostic(
                "contract.manifest.json",
                name,
                "",
                "manifest must be valid UTF-8 JSON without a BOM",
            )
        ]
    if not isinstance(parsed, dict):
        return None, exact_bytes, [
            _project_diagnostic(
                "contract.manifest.document",
                name,
                "",
                "manifest root must be a JSON object",
            )
        ]
    return parsed, exact_bytes, []


def _validate_request_handoff(
    request: object,
) -> tuple[
    bootstrap_session.ProjectOpenRequestV1 | None,
    tuple[bootstrap_session.BootstrapSessionDiagnostic, ...],
]:
    if not isinstance(request, bootstrap_session.ProjectOpenRequestV1):
        return None, (
            _control_plane_diagnostic(
                "bootstrap.inspection.request-invalid",
                "/request",
                "project-open request has an unsupported type",
            ),
        )
    distribution = request.verified_distribution
    if not isinstance(
        distribution, distribution_verifier.VerifiedInstalledDistribution
    ):
        return None, (
            _control_plane_diagnostic(
                "bootstrap.inspection.distribution-handoff-invalid",
                "/request/verifiedDistribution",
                "project-open requires a verified installed Distribution handoff",
            ),
        )
    try:
        parsed_manifest_bytes = json.loads(
            distribution.manifest_bytes.decode("utf-8")
        )
    except (AttributeError, UnicodeDecodeError, json.JSONDecodeError):
        parsed_manifest_bytes = None
    if (
        not isinstance(distribution.generation_root, Path)
        or not isinstance(distribution.manifest, dict)
        or not isinstance(distribution.manifest_bytes, bytes)
        or not isinstance(distribution.manifest_integrity, dict)
        or not isinstance(distribution.engine_generation_id, str)
        or not isinstance(distribution.manifest.get("bundledPackages"), list)
        or distribution.manifest.get("engineGenerationId")
        != distribution.engine_generation_id
        or parsed_manifest_bytes != distribution.manifest
        or contracts.compute_bytes_integrity(distribution.manifest_bytes)
        != distribution.manifest_integrity
    ):
        return None, (
            _control_plane_diagnostic(
                "bootstrap.inspection.distribution-handoff-invalid",
                "/request/verifiedDistribution",
                "verified Distribution handoff is internally inconsistent",
            ),
        )
    if not isinstance(
        request.host_profile_snapshot, effective_session.HostProfileSnapshot
    ):
        return None, (
            _control_plane_diagnostic(
                "bootstrap.inspection.host-profile-handoff-invalid",
                "/request/hostProfileSnapshot",
                "project-open requires one exact Host Profile snapshot",
            ),
        )
    assert isinstance(parsed_manifest_bytes, dict)
    captured_distribution = distribution_verifier.VerifiedInstalledDistribution(
        engine_generation_id=distribution.engine_generation_id,
        generation_root=distribution.generation_root,
        manifest=parsed_manifest_bytes,
        manifest_bytes=bytes(distribution.manifest_bytes),
        manifest_integrity=dict(distribution.manifest_integrity),
    )
    return bootstrap_session.ProjectOpenRequestV1(
        project_root=request.project_root,
        verified_distribution=captured_distribution,
        host_profile_snapshot=request.host_profile_snapshot,
        local_sources=request.local_sources,
    ), ()


def _changed_snapshot_diagnostics(
    snapshot: bootstrap_session.ProjectPackageSnapshotV1,
) -> tuple[bootstrap_session.BootstrapSessionDiagnostic, ...]:
    diagnostics: list[bootstrap_session.BootstrapSessionDiagnostic] = []
    for name, expected in (
        (contracts.PROJECT_MANIFEST_NAME, snapshot.project_manifest_bytes),
        (contracts.PACKAGE_LOCK_NAME, snapshot.lock_bytes),
    ):
        path = snapshot.project_root / name
        try:
            actual = None if _is_link(path) or not path.is_file() else path.read_bytes()
        except OSError:
            actual = None
        if actual != expected:
            diagnostics.append(
                _project_diagnostic(
                    "bootstrap.project.contract-changed",
                    name,
                    "",
                    (
                        "project package contract changed while session "
                        "evidence was collected"
                    ),
                )
            )
    return tuple(diagnostics)


def inspect_project_open_request(
    request: object,
    validators: contracts.ContractValidators,
) -> bootstrap_session.ProjectOpenInspectionV1:
    """Inspect one exact Project package graph without resolving or mutating it."""

    typed_request, handoff_diagnostics = _validate_request_handoff(request)
    if typed_request is None:
        return _inspection(
            owners=(
                bootstrap_session.InspectionFailureOwnerV1.CONTROL_PLANE,
            ),
            diagnostics=handoff_diagnostics,
        )

    project_root, root_diagnostics = _canonical_project_root(
        typed_request.project_root
    )
    if project_root is None:
        owner = (
            bootstrap_session.InspectionFailureOwnerV1.CONTROL_PLANE
            if root_diagnostics[0].code.endswith("root-invalid")
            else bootstrap_session.InspectionFailureOwnerV1.PROJECT
        )
        return _inspection(owners=(owner,), diagnostics=root_diagnostics)

    project, project_bytes, project_diagnostics = _read_exact_document(
        project_root, contracts.PROJECT_MANIFEST_NAME
    )
    lock, lock_bytes, lock_diagnostics = _read_exact_document(
        project_root, contracts.PACKAGE_LOCK_NAME
    )
    diagnostics = [*project_diagnostics, *lock_diagnostics]
    if project is not None:
        diagnostics.extend(
            _public_contract_diagnostic(value)
            for value in contracts.validate_manifest_data(
                project, contracts.PROJECT_MANIFEST_NAME, validators
            )
        )
    if lock is not None:
        diagnostics.extend(
            _public_contract_diagnostic(value)
            for value in contracts.validate_manifest_data(
                lock, contracts.PACKAGE_LOCK_NAME, validators
            )
        )
    if diagnostics:
        return _inspection(
            owners=(bootstrap_session.InspectionFailureOwnerV1.PROJECT,),
            diagnostics=diagnostics,
        )

    assert project is not None
    assert project_bytes is not None
    assert lock is not None
    assert lock_bytes is not None
    snapshot = bootstrap_session.ProjectPackageSnapshotV1(
        project_root=project_root,
        project_manifest=project,
        project_manifest_bytes=project_bytes,
        lock=lock,
        lock_bytes=lock_bytes,
    )
    discovered = bootstrap_candidate_locations.discover_bootstrap_candidates(
        project_root,
        typed_request.verified_distribution,
        lock,
        typed_request.local_sources,
        validators,
    )
    if not discovered.succeeded:
        return _inspection(
            snapshot=snapshot,
            owners=discovered.failure_owners,
            diagnostics=discovered.diagnostics,
        )

    try:
        session = effective_session.plan_effective_session(
            typed_request.verified_distribution.manifest,
            project,
            lock,
            discovered.candidates,
            typed_request.host_profile_snapshot,
            validators,
        )
    except (AssertionError, KeyError, TypeError, ValueError):
        return _inspection(
            snapshot=snapshot,
            owners=(
                bootstrap_session.InspectionFailureOwnerV1.CONTROL_PLANE,
            ),
            diagnostics=(
                _control_plane_diagnostic(
                    "bootstrap.inspection.control-plane-failed",
                    "/inspection/effectiveSession",
                    "session planning failed at a verified control-plane boundary",
                ),
            ),
        )

    changed_diagnostics = _changed_snapshot_diagnostics(snapshot)
    if changed_diagnostics:
        return _inspection(
            snapshot=snapshot,
            owners=(bootstrap_session.InspectionFailureOwnerV1.PROJECT,),
            diagnostics=changed_diagnostics,
        )

    session_owners: tuple[bootstrap_session.InspectionFailureOwnerV1, ...] = ()
    session_diagnostics: tuple[bootstrap_session.BootstrapSessionDiagnostic, ...] = ()
    if not session.succeeded:
        owner = (
            bootstrap_session.InspectionFailureOwnerV1.DISTRIBUTION
            if session.state is effective_session.EffectiveSessionState.REPAIR_REQUIRED
            else bootstrap_session.InspectionFailureOwnerV1.PROJECT
        )
        session_owners = (owner,)
        session_diagnostics = tuple(
            bootstrap_session.BootstrapSessionDiagnostic(
                value.code,
                value.manifest_path,
                value.pointer,
                value.message,
            )
            for value in session.diagnostics
        )
    return _inspection(
        snapshot=snapshot,
        session=session,
        owners=session_owners,
        diagnostics=session_diagnostics,
    )


__all__ = ["inspect_project_open_request"]
