"""Run the exact built Host in registration-only verification mode."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_artifact_collection
from tools import host_process
from tools import host_registration_request
from tools import host_registration_snapshot


HOST_REGISTRATION_VERIFICATION_ARGUMENT = (
    host_registration_request.HOST_REGISTRATION_VERIFICATION_ARGUMENT
)
DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES = (
    host_registration_request.DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES
)
HostRegistrationVerificationRequestV1 = (
    host_registration_request.HostRegistrationVerificationRequestV1
)
StagedHostRegistrationVerificationRequestV1 = (
    host_registration_request.StagedHostRegistrationVerificationRequestV1
)
_MANIFEST_PATH = "host-registration-verification"


@dataclass(frozen=True)
class HostRegistrationVerificationProcessEvidence:
    """Stable restricted-process facts without retaining log bytes."""

    arguments: tuple[str, ...]
    exit_code: int
    stdout_size: int
    stderr_size: int


@dataclass(frozen=True)
class HostRegistrationVerificationOutcomeV1:
    """Atomic outcome: one complete snapshot or owning diagnostics."""

    snapshot: host_registration_snapshot.HostRegistrationSnapshot | None
    process: HostRegistrationVerificationProcessEvidence | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.snapshot is not None and not self.diagnostics


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
        manifest_path=_MANIFEST_PATH,
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
    process: HostRegistrationVerificationProcessEvidence | None = None,
) -> HostRegistrationVerificationOutcomeV1:
    return HostRegistrationVerificationOutcomeV1(
        snapshot=None,
        process=process,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _run_registration_verification(
    request: Any,
    validators: contracts.ContractValidators,
) -> HostRegistrationVerificationOutcomeV1:
    normalized, diagnostics = (
        host_registration_request.validate_host_registration_verification_request(
            request
        )
    )
    if diagnostics:
        return _failure(diagnostics)
    assert normalized is not None

    if isinstance(request, StagedHostRegistrationVerificationRequestV1):
        artifact_diagnostics = (
            host_artifact_collection.verify_collected_host_artifact(
                request.artifact,
                verify_source=False,
            )
        )
        if artifact_diagnostics:
            return _failure(artifact_diagnostics)

    arguments = (
        str(normalized.artifact_path),
        HOST_REGISTRATION_VERIFICATION_ARGUMENT,
    )
    try:
        completed = host_process.run_bounded_host_process(
            arguments,
            normalized.environment,
            request.timeout_seconds,
            request.max_snapshot_bytes,
        )
    except host_process.HostProcessTimeout:
        return _failure(
            [
                _diagnostic(
                    "host-verification.timeout",
                    "/process",
                    "restricted Host verification exceeded its explicit timeout",
                )
            ]
        )
    except (host_process.HostProcessSpawnFailure, OSError):
        return _failure(
            [
                _diagnostic(
                    "host-verification.spawn-failed",
                    "/process",
                    "could not start the exact Host artifact",
                )
            ]
        )

    process = HostRegistrationVerificationProcessEvidence(
        arguments=arguments,
        exit_code=completed.return_code,
        stdout_size=completed.stdout_size,
        stderr_size=completed.stderr_size,
    )
    if isinstance(request, StagedHostRegistrationVerificationRequestV1):
        artifact_diagnostics = (
            host_artifact_collection.verify_collected_host_artifact(
                request.artifact,
                verify_source=False,
            )
        )
        if artifact_diagnostics:
            return _failure(artifact_diagnostics, process)
    if completed.limit_exceeded is not None:
        return _failure(
            [
                _diagnostic(
                    (
                        "host-verification.snapshot-too-large"
                        if completed.limit_exceeded == "stdout"
                        else "host-verification.stderr-too-large"
                    ),
                    f"/process/{completed.limit_exceeded}",
                    "restricted Host output exceeds its explicit byte limit",
                )
            ],
            process,
        )
    if completed.return_code != 0:
        return _failure(
            [
                _diagnostic(
                    "host-verification.process-failed",
                    "/process",
                    f"restricted Host returned exit code {completed.return_code}",
                )
            ],
            process,
        )
    if completed.stderr:
        return _failure(
            [
                _diagnostic(
                    "host-verification.unexpected-stderr",
                    "/process/stderr",
                    "successful restricted Host verification must not write stderr",
                )
            ],
            process,
        )
    parsed = host_registration_snapshot.parse_host_registration_snapshot_bytes(
        completed.stdout,
        validators,
        expected_generation_id=request.expected_generation_id,
        expected_host_activation_blueprint_sha256=(
            request.expected_host_activation_blueprint_sha256
        ),
    )
    if not parsed.succeeded:
        return _failure(parsed.diagnostics, process)
    assert parsed.snapshot is not None
    return HostRegistrationVerificationOutcomeV1(
        snapshot=parsed.snapshot,
        process=process,
        diagnostics=(),
    )


def run_host_registration_verification(
    request: Any,
    validators: contracts.ContractValidators,
) -> HostRegistrationVerificationOutcomeV1:
    """Execute the exact final Host target's registration recorder."""

    if not isinstance(request, HostRegistrationVerificationRequestV1):
        return _failure(
            [
                _diagnostic(
                    "host-verification.request-invalid",
                    "",
                    "verification request must use HostRegistrationVerificationRequestV1",
                )
            ]
        )
    return _run_registration_verification(request, validators)


def run_staged_host_registration_verification(
    request: Any,
    validators: contracts.ContractValidators,
) -> HostRegistrationVerificationOutcomeV1:
    """Execute a collector-owned staged Host without fabricated CMake evidence."""

    if not isinstance(request, StagedHostRegistrationVerificationRequestV1):
        return _failure(
            [
                _diagnostic(
                    "host-verification.request-invalid",
                    "",
                    (
                        "staged verification request must use "
                        "StagedHostRegistrationVerificationRequestV1"
                    ),
                )
            ]
        )
    return _run_registration_verification(request, validators)


__all__ = [
    "DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES",
    "HOST_REGISTRATION_VERIFICATION_ARGUMENT",
    "HostRegistrationVerificationOutcomeV1",
    "HostRegistrationVerificationProcessEvidence",
    "HostRegistrationVerificationRequestV1",
    "StagedHostRegistrationVerificationRequestV1",
    "run_host_registration_verification",
    "run_staged_host_registration_verification",
]
