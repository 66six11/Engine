"""Collect, verify, and atomically publish one Host executable binding."""

from __future__ import annotations

import os
import tempfile
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_artifact_collection
from tools import host_binding_assembly
from tools import host_binding_generation_verifier as generation_verifier
from tools import host_binding_publication_io as publication_io
from tools import host_binding_publication_model as publication_model
from tools import host_build_publication
from tools import host_cmake_target
from tools import host_cmake_toolchain
from tools import host_executable_binding as binding
from tools import host_registration_cross_verifier
from tools import host_registration_verification


HOST_BINDING_GENERATIONS_DIRECTORY = (
    publication_io.HOST_BINDING_GENERATIONS_DIRECTORY
)
HostExecutableBindingPublicationRequestV1 = (
    publication_model.HostExecutableBindingPublicationRequestV1
)
HostExecutableBindingPublicationReceipt = (
    publication_model.HostExecutableBindingPublicationReceipt
)
HostExecutableBindingPublicationResult = (
    publication_model.HostExecutableBindingPublicationResult
)
_diagnostic = publication_model.diagnostic
_failure = publication_model.failure_result


class _PublicationFailure(Exception):
    def __init__(self, diagnostic: contracts.Diagnostic) -> None:
        super().__init__(diagnostic.message)
        self.diagnostic = diagnostic


class _PublicationDiagnosticsFailure(Exception):
    def __init__(self, diagnostics: Iterable[contracts.Diagnostic]) -> None:
        super().__init__()
        self.diagnostics = tuple(diagnostics)


def _raise(code: str, pointer: str, message: str) -> None:
    raise _PublicationFailure(_diagnostic(code, pointer, message))


def _raise_diagnostics(diagnostics: Iterable[contracts.Diagnostic]) -> None:
    raise _PublicationDiagnosticsFailure(diagnostics)


def _cleanup_staging(path: Path | None) -> contracts.Diagnostic | None:
    error = publication_io.cleanup_staging(path)
    if error is None:
        return None
    return _diagnostic(error.code, error.pointer, error.message)


def _validate_request(
    request: Any,
    validators: contracts.ContractValidators,
) -> tuple[HostExecutableBindingPublicationRequestV1 | None, list[contracts.Diagnostic]]:
    if not isinstance(request, HostExecutableBindingPublicationRequestV1):
        return None, [
            _diagnostic(
                "host-binding.publication.request-invalid",
                "",
                "publication requires HostExecutableBindingPublicationRequestV1",
            )
        ]
    diagnostics = host_build_publication.validate_final_host_publications(
        request.template_root,
        request.template_generation,
        request.composition_root,
        request.composition_generation,
    )
    if not isinstance(request.target, host_cmake_target.HostCMakeTargetEvidence):
        diagnostics.append(
            _diagnostic(
                "host-binding.cmake.evidence-invalid",
                "/target",
                "publication requires final Host target evidence from #290",
            )
        )
    cross = host_registration_cross_verifier.verify_host_registration_cross_binding(
        request.composition_generation,
        request.registration_snapshot,
        validators,
    )
    diagnostics.extend(cross.diagnostics)
    return (request if not diagnostics else None), diagnostics


def _fresh_configured_target(
    target: host_cmake_target.HostCMakeTargetEvidence,
) -> host_cmake_toolchain.HostCMakeConfiguredTargetEvidence:
    result = host_cmake_toolchain.read_host_cmake_configured_target(
        target.build_root,
        target.configuration,
        target.target_name,
        require_artifact=True,
    )
    if not result.succeeded:
        _raise_diagnostics(result.diagnostics)
    assert result.evidence is not None
    return result.evidence


def _require_same_build_handoff(
    expected: host_cmake_target.HostCMakeTargetEvidence,
    actual: host_cmake_toolchain.HostCMakeConfiguredTargetEvidence,
) -> None:
    if actual.target != expected:
        _raise(
            "host-binding.cmake.handoff-stale",
            "/target",
            "latest File API target differs from the completed #290 handoff",
        )


def _publication_receipt(
    verified: generation_verifier.VerifiedHostBindingGeneration,
    *,
    reused: bool,
) -> HostExecutableBindingPublicationReceipt:
    return HostExecutableBindingPublicationReceipt(
        verified.receipt.binding_generation_id,
        verified.generation_path,
        verified.receipt,
        verified.snapshot,
        reused,
    )


def collect_and_publish_host_executable_binding(
    request: Any,
    publication_root: Any,
    validators: contracts.ContractValidators,
) -> HostExecutableBindingPublicationResult:
    """Publish exact staged bytes only after all four evidence classes agree."""

    staging_path: Path | None = None
    diagnostics: list[contracts.Diagnostic] = []
    try:
        validated, diagnostics = _validate_request(request, validators)
        if diagnostics:
            return _failure(diagnostics)
        assert validated is not None

        initial = _fresh_configured_target(validated.target)
        _require_same_build_handoff(validated.target, initial)
        generations = publication_io.prepare_publication_generations(
            publication_root
        )
        staging_path = Path(
            tempfile.mkdtemp(prefix=".asharia-host-binding-", dir=generations)
        )
        collected = host_artifact_collection.collect_host_artifact(
            initial.target, staging_path
        )
        if not collected.succeeded:
            _raise_diagnostics(collected.diagnostics)
        assert collected.artifact is not None

        observed = (
            host_registration_verification.run_staged_host_registration_verification(
                host_registration_verification.StagedHostRegistrationVerificationRequestV1(
                    artifact_root=staging_path,
                    artifact=collected.artifact,
                    expected_generation_id=(
                        validated.composition_generation.manifest.generation_id
                    ),
                    expected_host_activation_blueprint_sha256=(
                        validated.composition_generation.manifest.inputs.host_activation_blueprint_integrity.digest
                    ),
                    environment=validated.environment,
                    timeout_seconds=validated.verification_timeout_seconds,
                    max_snapshot_bytes=validated.max_snapshot_bytes,
                ),
                validators,
            )
        )
        if not observed.succeeded:
            _raise_diagnostics(observed.diagnostics)
        assert observed.snapshot is not None
        if observed.snapshot != validated.registration_snapshot:
            _raise(
                "host-binding.registration.handoff-mismatch",
                "/registrationSnapshot",
                "staged Host registrations differ from the #290 snapshot",
            )

        artifact_diagnostics = (
            host_artifact_collection.verify_collected_host_artifact(
                collected.artifact,
                verify_source=True,
            )
        )
        if artifact_diagnostics:
            _raise_diagnostics(artifact_diagnostics)
        refreshed = _fresh_configured_target(validated.target)
        if refreshed != initial:
            _raise(
                "host-binding.cmake.reply-drift",
                "/target/fileApi",
                "File API target or configured compiler changed during collection",
            )

        assembled = host_binding_assembly.assemble_host_executable_binding(
            validated.composition_generation,
            validated.template_generation,
            refreshed,
            collected.artifact,
            observed.snapshot,
            validators,
        )
        if not assembled.succeeded:
            _raise_diagnostics(assembled.diagnostics)
        assert assembled.assembly is not None
        receipt_bytes = binding.render_host_executable_binding_receipt(
            assembled.assembly.receipt
        ).encode("utf-8")
        publication_io.write_exact(
            staging_path / binding.HOST_REGISTRATION_SNAPSHOT_PATH,
            assembled.assembly.snapshot_bytes,
            "/registrationSnapshot",
            create_parent=True,
        )
        publication_io.write_exact(
            staging_path / binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME,
            receipt_bytes,
            "/receipt",
            create_parent=False,
        )
        staged = generation_verifier.verify_host_binding_generation_tree(
            staging_path,
            validated.composition_generation,
            validated.template_generation,
            validators,
            require_generation_directory_name=False,
        )
        if not staged.succeeded:
            _raise_diagnostics(staged.diagnostics)

        final_path = (
            generations / assembled.assembly.receipt.binding_generation_id
        )
        reused = False
        if os.path.lexists(final_path):
            existing = generation_verifier.verify_published_host_binding_generation(
                final_path,
                validated.composition_generation,
                validated.template_generation,
                validators,
            )
            if not existing.succeeded:
                _raise_diagnostics(existing.diagnostics)
            cleanup = _cleanup_staging(staging_path)
            staging_path = None
            if cleanup is not None:
                _raise_diagnostics((cleanup,))
            reused = True
        else:
            try:
                os.rename(staging_path, final_path)
                staging_path = None
            except OSError:
                if not os.path.lexists(final_path):
                    raise
                existing = (
                    generation_verifier.verify_published_host_binding_generation(
                        final_path,
                        validated.composition_generation,
                        validated.template_generation,
                        validators,
                    )
                )
                if not existing.succeeded:
                    _raise_diagnostics(existing.diagnostics)
                cleanup = _cleanup_staging(staging_path)
                staging_path = None
                if cleanup is not None:
                    _raise_diagnostics((cleanup,))
                reused = True

        committed = generation_verifier.verify_published_host_binding_generation(
            final_path,
            validated.composition_generation,
            validated.template_generation,
            validators,
        )
        if not committed.succeeded:
            _raise_diagnostics(committed.diagnostics)
        assert committed.verified is not None
        return HostExecutableBindingPublicationResult(
            _publication_receipt(committed.verified, reused=reused),
            (),
        )
    except _PublicationFailure as error:
        diagnostics = [error.diagnostic]
    except _PublicationDiagnosticsFailure as error:
        diagnostics = list(error.diagnostics)
    except publication_io.HostBindingPublicationIoError as error:
        diagnostics = [_diagnostic(error.code, error.pointer, error.message)]
    except OSError as error:
        diagnostics = [
            _diagnostic(
                "host-binding.publication.io-failed",
                "/publication",
                f"Host binding publication failed: {error}",
            )
        ]
    finally:
        cleanup = _cleanup_staging(staging_path)
        if cleanup is not None:
            diagnostics.append(cleanup)
    return _failure(diagnostics)


__all__ = [
    "HOST_BINDING_GENERATIONS_DIRECTORY",
    "HostExecutableBindingPublicationReceipt",
    "HostExecutableBindingPublicationRequestV1",
    "HostExecutableBindingPublicationResult",
    "collect_and_publish_host_executable_binding",
]
