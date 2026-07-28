"""Lightweight current published Host image admission for project open."""

from __future__ import annotations

import os
import stat
from dataclasses import dataclass, field, fields
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from tools import bootstrap_session as bootstrap
from tools import check_package_contracts as contracts
from tools import effective_session
from tools import host_binding_generation_verifier as binding_verifier
from tools import host_executable_binding as binding
from tools import static_composition_root as composition


@dataclass(frozen=True)
class CurrentHostPayloadV1:
    """Opaque current-image handoff retained only for bounded execution."""

    static_composition: composition.StaticCompositionRootGeneration
    verified_binding: binding_verifier.VerifiedHostBindingGeneration
    artifact_path: Path = field(repr=False)


def _ordered_diagnostics(
    values: Iterable[bootstrap.BootstrapSessionDiagnostic],
) -> tuple[bootstrap.BootstrapSessionDiagnostic, ...]:
    unique = {
        (value.manifest_path, value.pointer, value.code, value.message): value
        for value in values
    }
    return tuple(
        sorted(
            unique.values(),
            key=lambda value: (
                value.manifest_path,
                value.pointer,
                value.code,
                value.message,
            ),
        )
    )


def _public_diagnostic(
    value: contracts.Diagnostic | effective_session.EffectiveSessionDiagnostic,
) -> bootstrap.BootstrapSessionDiagnostic:
    return bootstrap.BootstrapSessionDiagnostic(
        value.code,
        value.manifest_path,
        value.pointer,
        value.message,
    )


def _observation(
    disposition: bootstrap.CurrentImageDispositionV1,
    diagnostics: Iterable[bootstrap.BootstrapSessionDiagnostic],
    *,
    current: effective_session.SessionIntegrity | None = None,
    payload: CurrentHostPayloadV1 | None = None,
) -> bootstrap.CurrentImageObservationV1:
    return bootstrap.CurrentImageObservationV1(
        disposition,
        current,
        _ordered_diagnostics(diagnostics),
        payload,
    )


def _same_integrity(left: Any, right: Any) -> bool:
    return (
        getattr(left, "algorithm", None),
        getattr(left, "digest", None),
    ) == (
        getattr(right, "algorithm", None),
        getattr(right, "digest", None),
    )


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


class _ArtifactMissing(Exception):
    pass


class _ArtifactInvalid(Exception):
    pass


def _validators_are_loaded(value: object) -> bool:
    return isinstance(value, contracts.ContractValidators) and all(
        isinstance(
            getattr(value, item.name, None),
            contracts.Draft202012Validator,
        )
        for item in fields(contracts.ContractValidators)
    )


def _lightweight_artifact_observation(
    verified: binding_verifier.VerifiedHostBindingGeneration,
) -> Path:
    """Observe path/type/size only; deep byte hashing belongs to publication/repair."""

    relative = PurePosixPath(verified.receipt.artifact.path)
    if relative.is_absolute() or any(
        part in {"", ".", ".."} for part in relative.parts
    ):
        raise _ArtifactInvalid

    root = Path(os.path.abspath(verified.generation_path))
    try:
        root_status = root.lstat()
    except FileNotFoundError as error:
        raise _ArtifactMissing from error
    except OSError as error:
        raise _ArtifactInvalid from error
    if _is_link_or_reparse(root_status) or not stat.S_ISDIR(root_status.st_mode):
        raise _ArtifactInvalid

    current = root
    try:
        for index, component in enumerate(relative.parts):
            current /= component
            status = current.lstat()
            if _is_link_or_reparse(status):
                raise _ArtifactInvalid
            final = index == len(relative.parts) - 1
            if final:
                if not stat.S_ISREG(status.st_mode):
                    raise _ArtifactInvalid
                if status.st_size != verified.receipt.artifact.size:
                    raise _ArtifactInvalid
            elif not stat.S_ISDIR(status.st_mode):
                raise _ArtifactInvalid
    except FileNotFoundError as error:
        raise _ArtifactMissing from error
    except OSError as error:
        raise _ArtifactInvalid from error

    try:
        resolved_root = root.resolve(strict=True)
        resolved_artifact = current.resolve(strict=True)
        resolved_artifact.relative_to(resolved_root)
    except (OSError, ValueError) as error:
        raise _ArtifactInvalid from error
    return resolved_artifact


def current_host_artifact_for_execution(payload: object) -> Path | None:
    """Recheck that an admitted payload still names its binding-owned artifact."""

    if (
        not isinstance(payload, CurrentHostPayloadV1)
        or not isinstance(
            payload.static_composition,
            composition.StaticCompositionRootGeneration,
        )
        or not isinstance(
            payload.verified_binding,
            binding_verifier.VerifiedHostBindingGeneration,
        )
    ):
        return None
    manifest = payload.static_composition.manifest
    receipt = payload.verified_binding.receipt
    if (
        payload.verified_binding.generation_path.name
        != receipt.binding_generation_id
        or receipt.inputs.static_composition.generation_id
        != manifest.generation_id
        or not _same_integrity(
            receipt.inputs.static_composition.manifest_integrity,
            manifest.integrity,
        )
    ):
        return None
    try:
        artifact_path = _lightweight_artifact_observation(
            payload.verified_binding
        )
    except (_ArtifactMissing, _ArtifactInvalid):
        return None
    return artifact_path if payload.artifact_path == artifact_path else None


def observe_current_host_image(
    inspection: Any,
    static_composition: Any,
    verified_binding: Any,
    validators: contracts.ContractValidators,
) -> bootstrap.CurrentImageObservationV1:
    """Compare one deep-verified published Host binding with the desired session."""

    if not isinstance(inspection, bootstrap.ProjectOpenInspectionV1):
        return _observation(
            bootstrap.CurrentImageDispositionV1.INVALID,
            (
                bootstrap.diagnostic(
                    "bootstrap.image.inspection-invalid",
                    "/currentImage/inspection",
                    "current-image admission requires ProjectOpenInspectionV1",
                ),
            ),
        )
    if not _validators_are_loaded(validators):
        return _observation(
            bootstrap.CurrentImageDispositionV1.INVALID,
            (
                bootstrap.diagnostic(
                    "bootstrap.image.validators-invalid",
                    "/currentImage/validators",
                    "current-image admission requires loaded contract validators",
                ),
            ),
        )
    session = inspection.effective_session
    if session is None or not session.succeeded or session.plan is None:
        return _observation(
            bootstrap.CurrentImageDispositionV1.INVALID,
            (
                bootstrap.diagnostic(
                    "bootstrap.image.session-not-ready",
                    "/currentImage/effectiveSession",
                    "current-image admission requires one Ready Effective Session",
                ),
            ),
        )
    desired = session.plan

    if static_composition is None or verified_binding is None:
        return _observation(
            bootstrap.CurrentImageDispositionV1.MISSING,
            (
                bootstrap.diagnostic(
                    "bootstrap.image.generation-missing",
                    "/currentImage",
                    "the current project Host generation has not been published",
                ),
            ),
        )
    if not isinstance(
        static_composition, composition.StaticCompositionRootGeneration
    ) or not isinstance(
        verified_binding, binding_verifier.VerifiedHostBindingGeneration
    ):
        return _observation(
            bootstrap.CurrentImageDispositionV1.INVALID,
            (
                bootstrap.diagnostic(
                    "bootstrap.image.handoff-invalid",
                    "/currentImage",
                    (
                        "current-image admission requires typed composition and "
                        "verified binding handoffs"
                    ),
                ),
            ),
        )

    validation_diagnostics = [
        _public_diagnostic(value)
        for value in composition.validate_static_composition_root_generation(
            static_composition, validators
        )
    ]
    validation_diagnostics.extend(
        _public_diagnostic(value)
        for value in binding.validate_host_executable_binding_receipt_data(
            verified_binding.receipt, validators
        )
    )
    if validation_diagnostics:
        return _observation(
            bootstrap.CurrentImageDispositionV1.INVALID,
            validation_diagnostics,
        )

    manifest = static_composition.manifest
    receipt = verified_binding.receipt
    if verified_binding.generation_path.name != receipt.binding_generation_id:
        return _observation(
            bootstrap.CurrentImageDispositionV1.INVALID,
            (
                bootstrap.diagnostic(
                    "bootstrap.image.publication-path-invalid",
                    "/currentImage/bindingGenerationId",
                    (
                        "published Host generation directory does not match its "
                        "binding generation ID"
                    ),
                ),
            ),
        )
    configuration = desired.verified_graph.distribution.get("context", {}).get(
        "configuration"
    )
    session_fingerprint = manifest.inputs.effective_session_integrity
    current_integrity = effective_session.SessionIntegrity(
        session_fingerprint.algorithm,
        session_fingerprint.digest,
    )
    composition_identity = (
        _same_integrity(session_fingerprint, desired.session_fingerprint)
        and manifest.engine_generation_id
        == desired.verified_graph.engine_generation_id
        and manifest.host_kind == desired.host_kind
        and manifest.target_platform == desired.target_platform
        and manifest.configuration == configuration
    )
    receipt_identity = (
        receipt.inputs.static_composition.generation_id
        == manifest.generation_id
        and _same_integrity(
            receipt.inputs.static_composition.manifest_integrity,
            manifest.integrity,
        )
        and _same_integrity(
            receipt.inputs.host_activation_blueprint_integrity,
            manifest.inputs.host_activation_blueprint_integrity,
        )
        and receipt.host.engine_generation_id == manifest.engine_generation_id
        and receipt.host.host_kind == manifest.host_kind
        and receipt.host.target_platform == manifest.target_platform
        and receipt.build.configuration == manifest.configuration
        and receipt.target.target_type == "EXECUTABLE"
    )
    if not composition_identity or not receipt_identity:
        return _observation(
            bootstrap.CurrentImageDispositionV1.STALE,
            (
                bootstrap.diagnostic(
                    "bootstrap.image.identity-stale",
                    "/currentImage/identity",
                    "published Host identity does not match the desired Effective Session",
                ),
            ),
            current=current_integrity,
        )

    try:
        artifact_path = _lightweight_artifact_observation(verified_binding)
    except _ArtifactMissing:
        return _observation(
            bootstrap.CurrentImageDispositionV1.MISSING,
            (
                bootstrap.diagnostic(
                    "bootstrap.image.artifact-missing",
                    "/currentImage/artifact",
                    "the published project Host artifact is missing",
                ),
            ),
            current=current_integrity,
        )
    except _ArtifactInvalid:
        return _observation(
            bootstrap.CurrentImageDispositionV1.INVALID,
            (
                bootstrap.diagnostic(
                    "bootstrap.image.artifact-invalid",
                    "/currentImage/artifact",
                    (
                        "the published project Host artifact is not a regular file "
                        "of the recorded size"
                    ),
                ),
            ),
            current=current_integrity,
        )

    return _observation(
        bootstrap.CurrentImageDispositionV1.CURRENT,
        (),
        current=current_integrity,
        payload=CurrentHostPayloadV1(
            static_composition,
            verified_binding,
            artifact_path,
        ),
    )


__all__ = [
    "CurrentHostPayloadV1",
    "current_host_artifact_for_execution",
    "observe_current_host_image",
]
