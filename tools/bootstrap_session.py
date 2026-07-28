"""Pure Bootstrap project-open evidence and state reduction."""

from __future__ import annotations

import json
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Any, Iterable

from tools import effective_session
from tools import engine_distribution_repair_verifier as distribution_verifier
from tools import package_candidate_discovery


BOOTSTRAP_SESSION_REPORT_NAME = "asharia.bootstrap-session.json"
BOOTSTRAP_SESSION_SCHEMA = "com.asharia.bootstrap-session"
BOOTSTRAP_SESSION_SCHEMA_VERSION = 1


class BootstrapSessionState(str, Enum):
    NO_PROJECT = "NoProject"
    OPENING = "Opening"
    READY = "Ready"
    PENDING_BUILD = "PendingBuild"
    PENDING_RESTART = "PendingRestart"
    REPAIR_REQUIRED = "RepairRequired"
    UPGRADE_REQUIRED = "UpgradeRequired"
    SAFE_MODE = "SafeMode"
    FATAL_DISTRIBUTION_ERROR = "FatalDistributionError"


class BootstrapNextAction(str, Enum):
    SELECT_PROJECT = "SelectProject"
    INSPECT_PROJECT = "InspectProject"
    ACTIVATE_PROJECT_PROFILE = "ActivateProjectProfile"
    BUILD_PROJECT_HOST = "BuildProjectHost"
    RESTART_EDITOR = "RestartEditor"
    REPAIR_DISTRIBUTION = "RepairDistribution"
    UPGRADE_ENGINE = "UpgradeEngine"
    OPEN_SAFE_MODE = "OpenSafeMode"
    REPAIR_EDITOR_IMAGE = "RepairEditorImage"


class InspectionFailureOwnerV1(str, Enum):
    DISTRIBUTION = "Distribution"
    PROJECT = "Project"
    CONTROL_PLANE = "ControlPlane"


class CurrentImageDispositionV1(str, Enum):
    MISSING = "Missing"
    STALE = "Stale"
    INVALID = "Invalid"
    CURRENT = "Current"


class ProjectBootstrapDispositionV1(str, Enum):
    SUCCEEDED = "Succeeded"
    PROJECT_REJECTED = "ProjectRejected"
    HOST_FAILED = "HostFailed"


@dataclass(frozen=True, order=True)
class BootstrapSessionDiagnostic:
    code: str
    manifest_path: str
    pointer: str
    message: str

    def render(self) -> str:
        return (
            f"{self.manifest_path}{self.pointer}: "
            f"[{self.code}] {self.message}"
        )


@dataclass(frozen=True)
class ProjectOpenRequestV1:
    project_root: Path
    verified_distribution: distribution_verifier.VerifiedInstalledDistribution
    host_profile_snapshot: effective_session.HostProfileSnapshot
    local_sources: tuple[
        package_candidate_discovery.LocalCandidateLocation, ...
    ] = ()


@dataclass(frozen=True)
class ProjectPackageSnapshotV1:
    project_root: Path
    project_manifest: dict[str, Any]
    project_manifest_bytes: bytes
    lock: dict[str, Any]
    lock_bytes: bytes


@dataclass(frozen=True)
class ProjectOpenInspectionV1:
    package_snapshot: ProjectPackageSnapshotV1 | None
    effective_session: effective_session.EffectiveSessionResult | None
    failure_owners: tuple[InspectionFailureOwnerV1, ...]
    diagnostics: tuple[BootstrapSessionDiagnostic, ...]


@dataclass(frozen=True)
class CurrentImageObservationV1:
    disposition: CurrentImageDispositionV1
    current_session_integrity: effective_session.SessionIntegrity | None
    diagnostics: tuple[BootstrapSessionDiagnostic, ...]
    payload: object | None = None


@dataclass(frozen=True, order=True)
class ProjectBootstrapSummaryV1:
    project_name: str
    project_id: str
    asset_source_root_count: int


@dataclass(frozen=True)
class ProjectBootstrapRunObservationV1:
    disposition: ProjectBootstrapDispositionV1
    summary: ProjectBootstrapSummaryV1 | None
    exit_code: int | None
    stdout_size: int
    stderr_size: int
    diagnostics: tuple[BootstrapSessionDiagnostic, ...]


@dataclass(frozen=True)
class BootstrapSessionEvidenceV1:
    request: ProjectOpenRequestV1 | None
    inspection: ProjectOpenInspectionV1 | None = None
    current_image: CurrentImageObservationV1 | None = None
    project_bootstrap: ProjectBootstrapRunObservationV1 | None = None


@dataclass(frozen=True)
class BootstrapSessionResultV1:
    state: BootstrapSessionState
    next_action: BootstrapNextAction
    plan: effective_session.EffectiveSessionPlan | None
    project: ProjectBootstrapSummaryV1 | None
    desired_session_integrity: effective_session.SessionIntegrity | None
    current_session_integrity: effective_session.SessionIntegrity | None
    diagnostics: tuple[BootstrapSessionDiagnostic, ...]


_NEXT_ACTIONS = {
    BootstrapSessionState.NO_PROJECT: BootstrapNextAction.SELECT_PROJECT,
    BootstrapSessionState.OPENING: BootstrapNextAction.INSPECT_PROJECT,
    BootstrapSessionState.READY: BootstrapNextAction.ACTIVATE_PROJECT_PROFILE,
    BootstrapSessionState.PENDING_BUILD: BootstrapNextAction.BUILD_PROJECT_HOST,
    BootstrapSessionState.PENDING_RESTART: BootstrapNextAction.RESTART_EDITOR,
    BootstrapSessionState.REPAIR_REQUIRED: BootstrapNextAction.REPAIR_DISTRIBUTION,
    BootstrapSessionState.UPGRADE_REQUIRED: BootstrapNextAction.UPGRADE_ENGINE,
    BootstrapSessionState.SAFE_MODE: BootstrapNextAction.OPEN_SAFE_MODE,
    BootstrapSessionState.FATAL_DISTRIBUTION_ERROR: (
        BootstrapNextAction.REPAIR_EDITOR_IMAGE
    ),
}


def diagnostic(
    code: str,
    pointer: str,
    message: str,
    *,
    manifest_path: str = BOOTSTRAP_SESSION_REPORT_NAME,
) -> BootstrapSessionDiagnostic:
    return BootstrapSessionDiagnostic(code, manifest_path, pointer, message)


def _diagnostic_sort_key(
    value: BootstrapSessionDiagnostic,
) -> tuple[str, str, str, str]:
    return value.manifest_path, value.pointer, value.code, value.message


def _ordered_diagnostics(
    values: Iterable[BootstrapSessionDiagnostic],
) -> tuple[BootstrapSessionDiagnostic, ...]:
    unique = {
        (value.manifest_path, value.pointer, value.code, value.message): value
        for value in values
    }
    return tuple(sorted(unique.values(), key=_diagnostic_sort_key))


def _result(
    state: BootstrapSessionState,
    diagnostics: Iterable[BootstrapSessionDiagnostic] = (),
    *,
    plan: effective_session.EffectiveSessionPlan | None = None,
    project: ProjectBootstrapSummaryV1 | None = None,
    current: effective_session.SessionIntegrity | None = None,
) -> BootstrapSessionResultV1:
    desired = plan.session_fingerprint if plan is not None else None
    return BootstrapSessionResultV1(
        state=state,
        next_action=_NEXT_ACTIONS[state],
        plan=plan,
        project=project,
        desired_session_integrity=desired,
        current_session_integrity=current,
        diagnostics=_ordered_diagnostics(diagnostics),
    )


def _effective_session_diagnostics(
    result: effective_session.EffectiveSessionResult,
) -> tuple[BootstrapSessionDiagnostic, ...]:
    return tuple(
        BootstrapSessionDiagnostic(
            value.code,
            value.manifest_path,
            value.pointer,
            value.message,
        )
        for value in result.diagnostics
    )


def derive_bootstrap_session(
    evidence: BootstrapSessionEvidenceV1,
) -> BootstrapSessionResultV1:
    """Reduce ordered evidence without performing IO or guessing later states."""

    if evidence.request is None:
        return _result(BootstrapSessionState.NO_PROJECT)
    inspection = evidence.inspection
    if inspection is None:
        return _result(BootstrapSessionState.OPENING)

    diagnostics = list(inspection.diagnostics)
    owners = set(inspection.failure_owners)
    if InspectionFailureOwnerV1.CONTROL_PLANE in owners:
        return _result(
            BootstrapSessionState.FATAL_DISTRIBUTION_ERROR, diagnostics
        )
    if InspectionFailureOwnerV1.DISTRIBUTION in owners:
        return _result(BootstrapSessionState.REPAIR_REQUIRED, diagnostics)

    session = inspection.effective_session
    if session is not None and not session.succeeded:
        diagnostics.extend(_effective_session_diagnostics(session))
        state = {
            effective_session.EffectiveSessionState.REPAIR_REQUIRED: (
                BootstrapSessionState.REPAIR_REQUIRED
            ),
            effective_session.EffectiveSessionState.UPGRADE_REQUIRED: (
                BootstrapSessionState.UPGRADE_REQUIRED
            ),
            effective_session.EffectiveSessionState.SAFE_MODE: (
                BootstrapSessionState.SAFE_MODE
            ),
        }.get(session.state)
        if state is None:
            diagnostics.append(
                diagnostic(
                    "bootstrap.inspection.session-state-invalid",
                    "/inspection/effectiveSession",
                    "inspection returned an unsupported non-Ready session state",
                )
            )
            return _result(
                BootstrapSessionState.FATAL_DISTRIBUTION_ERROR, diagnostics
            )
        return _result(state, diagnostics)
    if InspectionFailureOwnerV1.PROJECT in owners:
        return _result(BootstrapSessionState.SAFE_MODE, diagnostics)
    if session is None or not session.succeeded or session.plan is None:
        diagnostics.append(
            diagnostic(
                "bootstrap.inspection.incomplete",
                "/inspection",
                "successful project inspection did not provide a Ready session",
            )
        )
        return _result(
            BootstrapSessionState.FATAL_DISTRIBUTION_ERROR, diagnostics
        )

    plan = session.plan
    current_image = evidence.current_image
    if current_image is None:
        return _result(BootstrapSessionState.OPENING, diagnostics, plan=plan)
    diagnostics.extend(current_image.diagnostics)
    if current_image.disposition is not CurrentImageDispositionV1.CURRENT:
        return _result(
            BootstrapSessionState.PENDING_BUILD,
            diagnostics,
            plan=plan,
            current=current_image.current_session_integrity,
        )

    project_bootstrap = evidence.project_bootstrap
    if project_bootstrap is None:
        return _result(
            BootstrapSessionState.OPENING,
            diagnostics,
            plan=plan,
            current=current_image.current_session_integrity,
        )
    diagnostics.extend(project_bootstrap.diagnostics)
    if (
        project_bootstrap.disposition
        is ProjectBootstrapDispositionV1.PROJECT_REJECTED
    ):
        return _result(
            BootstrapSessionState.SAFE_MODE,
            diagnostics,
            plan=plan,
            current=current_image.current_session_integrity,
        )
    if (
        project_bootstrap.disposition
        is ProjectBootstrapDispositionV1.HOST_FAILED
    ):
        return _result(
            BootstrapSessionState.FATAL_DISTRIBUTION_ERROR,
            diagnostics,
            plan=plan,
            current=current_image.current_session_integrity,
        )
    if project_bootstrap.summary is None:
        diagnostics.append(
            diagnostic(
                "bootstrap.host.summary-missing",
                "/projectBootstrap/summary",
                "successful Project Bootstrap observation omitted its summary",
            )
        )
        return _result(
            BootstrapSessionState.FATAL_DISTRIBUTION_ERROR,
            diagnostics,
            plan=plan,
            current=current_image.current_session_integrity,
        )
    return _result(
        BootstrapSessionState.READY,
        diagnostics,
        plan=plan,
        project=project_bootstrap.summary,
        current=current_image.current_session_integrity,
    )


def render_bootstrap_session_result(value: BootstrapSessionResultV1) -> bytes:
    """Render a deterministic path-free report for future UI/CLI consumers."""

    def integrity_data(
        item: effective_session.SessionIntegrity | None,
    ) -> dict[str, str] | None:
        if item is None:
            return None
        return {"algorithm": item.algorithm, "digest": item.digest}

    project = None
    if value.project is not None:
        project = {
            "projectName": value.project.project_name,
            "projectId": value.project.project_id,
            "assetSourceRootCount": value.project.asset_source_root_count,
        }
    data = {
        "schema": BOOTSTRAP_SESSION_SCHEMA,
        "schemaVersion": BOOTSTRAP_SESSION_SCHEMA_VERSION,
        "state": value.state.value,
        "nextAction": value.next_action.value,
        "desiredSessionIntegrity": integrity_data(
            value.desired_session_integrity
        ),
        "currentSessionIntegrity": integrity_data(
            value.current_session_integrity
        ),
        "project": project,
        "diagnostics": [
            {
                "code": item.code,
                "manifestPath": item.manifest_path,
                "pointer": item.pointer,
                "message": item.message,
            }
            for item in value.diagnostics
        ],
    }
    return (
        json.dumps(data, ensure_ascii=False, indent=2, separators=(",", ": "))
        + "\n"
    ).encode("utf-8")


__all__ = [
    "BOOTSTRAP_SESSION_REPORT_NAME",
    "BOOTSTRAP_SESSION_SCHEMA",
    "BOOTSTRAP_SESSION_SCHEMA_VERSION",
    "BootstrapNextAction",
    "BootstrapSessionDiagnostic",
    "BootstrapSessionEvidenceV1",
    "BootstrapSessionResultV1",
    "BootstrapSessionState",
    "CurrentImageDispositionV1",
    "CurrentImageObservationV1",
    "InspectionFailureOwnerV1",
    "ProjectBootstrapDispositionV1",
    "ProjectBootstrapRunObservationV1",
    "ProjectBootstrapSummaryV1",
    "ProjectOpenInspectionV1",
    "ProjectOpenRequestV1",
    "ProjectPackageSnapshotV1",
    "derive_bootstrap_session",
    "diagnostic",
    "render_bootstrap_session_result",
]
