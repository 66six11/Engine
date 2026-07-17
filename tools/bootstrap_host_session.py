"""Project-open orchestration across inspection, image admission, and Host run."""

from __future__ import annotations

from dataclasses import dataclass, field, fields
from pathlib import Path

from tools import bootstrap_current_host
from tools import bootstrap_project_host
from tools import bootstrap_project_inspection
from tools import bootstrap_session as bootstrap
from tools import check_package_contracts as contracts
from tools import effective_session
from tools import host_binding_generation_verifier as binding_verifier
from tools import host_process
from tools import static_composition_root as composition


@dataclass(frozen=True)
class BootstrapHostAdapterContextV1:
    """Explicit current image and process policy supplied by the Editor Image."""

    static_composition: composition.StaticCompositionRootGeneration | None
    verified_binding: binding_verifier.VerifiedHostBindingGeneration | None
    environment: tuple[tuple[str, str], ...] = field(repr=False)
    timeout_seconds: float = (
        bootstrap_project_host.DEFAULT_PROJECT_BOOTSTRAP_TIMEOUT_SECONDS
    )
    max_summary_bytes: int = (
        bootstrap_project_host.DEFAULT_MAX_PROJECT_BOOTSTRAP_SUMMARY_BYTES
    )
    max_stderr_bytes: int = host_process.MAX_HOST_STDERR_BYTES


def _missing_project_snapshot() -> bootstrap.ProjectBootstrapRunObservationV1:
    return bootstrap.ProjectBootstrapRunObservationV1(
        disposition=bootstrap.ProjectBootstrapDispositionV1.HOST_FAILED,
        summary=None,
        exit_code=None,
        stdout_size=0,
        stderr_size=0,
        diagnostics=(
            bootstrap.diagnostic(
                "bootstrap.host.project-snapshot-missing",
                "/projectBootstrap/projectRoot",
                "Ready inspection omitted its canonical project snapshot",
            ),
        ),
    )


def _control_plane_failure(
    request: object,
    code: str,
    pointer: str,
    message: str,
) -> bootstrap.BootstrapSessionResultV1:
    failure_diagnostic = bootstrap.diagnostic(code, pointer, message)
    if request is None:
        return bootstrap.BootstrapSessionResultV1(
            state=bootstrap.BootstrapSessionState.FATAL_DISTRIBUTION_ERROR,
            next_action=bootstrap.BootstrapNextAction.REPAIR_EDITOR_IMAGE,
            plan=None,
            project=None,
            desired_session_integrity=None,
            current_session_integrity=None,
            diagnostics=(failure_diagnostic,),
        )
    inspection = bootstrap.ProjectOpenInspectionV1(
        package_snapshot=None,
        effective_session=None,
        failure_owners=(bootstrap.InspectionFailureOwnerV1.CONTROL_PLANE,),
        diagnostics=(failure_diagnostic,),
    )
    return bootstrap.derive_bootstrap_session(
        bootstrap.BootstrapSessionEvidenceV1(
            request=request,
            inspection=inspection,
        )
    )


def _orchestration_inputs_failure(
    request: object,
    context: object,
    validators: object,
) -> bootstrap.BootstrapSessionResultV1 | None:
    if not isinstance(request, bootstrap.ProjectOpenRequestV1):
        return _control_plane_failure(
            request,
            "bootstrap.host.request-invalid",
            "/hostAdapter/request",
            "Host orchestration requires ProjectOpenRequestV1",
        )
    if not isinstance(context, BootstrapHostAdapterContextV1):
        return _control_plane_failure(
            request,
            "bootstrap.host.context-invalid",
            "/hostAdapter/context",
            "Host orchestration requires BootstrapHostAdapterContextV1",
        )
    context_values_valid = (
        (
            context.static_composition is None
            or isinstance(
                context.static_composition,
                composition.StaticCompositionRootGeneration,
            )
        )
        and (
            context.verified_binding is None
            or isinstance(
                context.verified_binding,
                binding_verifier.VerifiedHostBindingGeneration,
            )
        )
        and bootstrap_project_host.is_valid_project_bootstrap_process_policy(
            context.environment,
            context.timeout_seconds,
            context.max_summary_bytes,
            context.max_stderr_bytes,
        )
    )
    if not context_values_valid:
        return _control_plane_failure(
            request,
            "bootstrap.host.context-invalid",
            "/hostAdapter/context",
            "Host adapter context contains invalid image or process policy values",
        )
    validators_valid = isinstance(validators, contracts.ContractValidators) and all(
        isinstance(
            getattr(validators, value.name, None),
            contracts.Draft202012Validator,
        )
        for value in fields(contracts.ContractValidators)
    )
    if not validators_valid:
        return _control_plane_failure(
            request,
            "bootstrap.host.validators-invalid",
            "/hostAdapter/validators",
            "Host orchestration requires loaded package contract validators",
        )
    return None


def _inspection_handoff_is_valid(value: object) -> bool:
    return (
        isinstance(value, bootstrap.ProjectOpenInspectionV1)
        and (
            value.package_snapshot is None
            or isinstance(
                value.package_snapshot,
                bootstrap.ProjectPackageSnapshotV1,
            )
        )
        and (
            value.effective_session is None
            or _effective_session_handoff_is_valid(value.effective_session)
        )
        and isinstance(value.failure_owners, tuple)
        and all(
            isinstance(item, bootstrap.InspectionFailureOwnerV1)
            for item in value.failure_owners
        )
        and isinstance(value.diagnostics, tuple)
        and all(
            isinstance(item, bootstrap.BootstrapSessionDiagnostic)
            for item in value.diagnostics
        )
    )


def _effective_session_handoff_is_valid(value: object) -> bool:
    if not isinstance(value, effective_session.EffectiveSessionResult):
        return False
    if (
        not isinstance(value.state, effective_session.EffectiveSessionState)
        or not isinstance(value.diagnostics, tuple)
        or not all(
            isinstance(item, effective_session.EffectiveSessionDiagnostic)
            for item in value.diagnostics
        )
    ):
        return False
    if value.plan is None:
        return True
    plan = value.plan
    return (
        isinstance(plan, effective_session.EffectiveSessionPlan)
        and isinstance(
            plan.verified_graph,
            effective_session.VerifiedResolvedGraph,
        )
        and isinstance(plan.verified_graph.distribution, dict)
        and isinstance(plan.host_profile, dict)
        and isinstance(plan.host_profile.get("hostKind"), str)
        and isinstance(plan.host_profile.get("targetPlatform"), str)
        and isinstance(
            plan.session_fingerprint,
            effective_session.SessionIntegrity,
        )
    )


def _request_matches_inspected_root(
    request: bootstrap.ProjectOpenRequestV1,
    inspection: bootstrap.ProjectOpenInspectionV1,
) -> bool:
    snapshot = inspection.package_snapshot
    if snapshot is None or not isinstance(snapshot.project_root, Path):
        return False
    if not isinstance(request.project_root, Path):
        return False
    try:
        requested_root = request.project_root.resolve(strict=True)
    except (OSError, RuntimeError):
        return False
    return requested_root == snapshot.project_root


def _advance_inspected_bootstrap_session(
    request: bootstrap.ProjectOpenRequestV1,
    inspection: bootstrap.ProjectOpenInspectionV1,
    context: BootstrapHostAdapterContextV1,
    validators: contracts.ContractValidators,
) -> bootstrap.BootstrapSessionResultV1:
    """Advance only after inspection, reducing before every later side effect."""

    inputs_failure = _orchestration_inputs_failure(request, context, validators)
    if inputs_failure is not None:
        return inputs_failure
    if not _inspection_handoff_is_valid(inspection):
        return _control_plane_failure(
            request,
            "bootstrap.host.inspection-invalid",
            "/hostAdapter/inspection",
            "Host orchestration requires one typed project-open inspection",
        )
    evidence = bootstrap.BootstrapSessionEvidenceV1(
        request=request,
        inspection=inspection,
    )
    reduced = bootstrap.derive_bootstrap_session(evidence)
    if reduced.state is not bootstrap.BootstrapSessionState.OPENING:
        return reduced
    if not _request_matches_inspected_root(request, inspection):
        return _control_plane_failure(
            request,
            "bootstrap.host.request-inspection-mismatch",
            "/hostAdapter/request/projectRoot",
            "project-open request does not match the inspected canonical root",
        )

    current = bootstrap_current_host.observe_current_host_image(
        inspection,
        context.static_composition,
        context.verified_binding,
        validators,
    )
    evidence = bootstrap.BootstrapSessionEvidenceV1(
        request=request,
        inspection=inspection,
        current_image=current,
    )
    reduced = bootstrap.derive_bootstrap_session(evidence)
    if reduced.state is not bootstrap.BootstrapSessionState.OPENING:
        return reduced

    if inspection.package_snapshot is None:
        run = _missing_project_snapshot()
    else:
        run = bootstrap_project_host.run_project_bootstrap_host(
            inspection.package_snapshot.project_root,
            current,
            context.environment,
            timeout_seconds=context.timeout_seconds,
            max_summary_bytes=context.max_summary_bytes,
            max_stderr_bytes=context.max_stderr_bytes,
        )
    return bootstrap.derive_bootstrap_session(
        bootstrap.BootstrapSessionEvidenceV1(
            request=request,
            inspection=inspection,
            current_image=current,
            project_bootstrap=run,
        )
    )


def open_bootstrap_session(
    request: bootstrap.ProjectOpenRequestV1 | None,
    context: BootstrapHostAdapterContextV1,
    validators: contracts.ContractValidators,
) -> bootstrap.BootstrapSessionResultV1:
    """Inspect and advance one project-open request through the fixed Host."""

    initial = bootstrap.derive_bootstrap_session(
        bootstrap.BootstrapSessionEvidenceV1(request=request)
    )
    if initial.state is not bootstrap.BootstrapSessionState.OPENING:
        return initial
    assert request is not None
    inputs_failure = _orchestration_inputs_failure(request, context, validators)
    if inputs_failure is not None:
        return inputs_failure
    inspection = bootstrap_project_inspection.inspect_project_open_request(
        request, validators
    )
    return _advance_inspected_bootstrap_session(
        request,
        inspection,
        context,
        validators,
    )


__all__ = [
    "BootstrapHostAdapterContextV1",
    "open_bootstrap_session",
]
