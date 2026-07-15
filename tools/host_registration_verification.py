"""Run the exact built Host in registration-only verification mode."""

from __future__ import annotations

import math
import os
import stat
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_cmake_target
from tools import host_registration_snapshot


HOST_REGISTRATION_VERIFICATION_ARGUMENT = (
    "--asharia-verify-static-registration"
)
DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES = 4 * 1024 * 1024
_MAX_TIMEOUT_SECONDS = 300.0
_MANIFEST_PATH = "host-registration-verification"


@dataclass(frozen=True)
class HostRegistrationVerificationRequestV1:
    """Exact artifact, expected generation, and controlled process inputs."""

    target: host_cmake_target.HostCMakeTargetEvidence
    expected_generation_id: str
    expected_host_activation_blueprint_sha256: str
    environment: tuple[tuple[str, str], ...] = field(repr=False)
    timeout_seconds: float = 60.0
    max_snapshot_bytes: int = DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES


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


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def _environment(value: Any) -> tuple[dict[str, str] | None, list[contracts.Diagnostic]]:
    if not isinstance(value, tuple):
        return None, [
            _diagnostic(
                "host-verification.environment-invalid",
                "/environment",
                "environment must be one explicit tuple of key/value pairs",
            )
        ]
    result: dict[str, str] = {}
    folded_keys: set[str] = set()
    diagnostics: list[contracts.Diagnostic] = []
    for index, item in enumerate(value):
        if (
            not isinstance(item, tuple)
            or len(item) != 2
            or not isinstance(item[0], str)
            or not isinstance(item[1], str)
            or not item[0]
            or "=" in item[0]
            or "\0" in item[0]
            or "\0" in item[1]
        ):
            diagnostics.append(
                _diagnostic(
                    "host-verification.environment-invalid",
                    f"/environment/{index}",
                    "environment entries must be non-empty string key/value pairs",
                )
            )
            continue
        folded = item[0].casefold()
        if folded in folded_keys:
            diagnostics.append(
                _diagnostic(
                    "host-verification.environment-duplicate",
                    f"/environment/{index}",
                    f"environment key '{item[0]}' is duplicated case-insensitively",
                )
            )
            continue
        folded_keys.add(folded)
        result[item[0]] = item[1]
    return (result if not diagnostics else None), diagnostics


def _validate_request(
    request: Any,
) -> tuple[
    tuple[Path, Path, dict[str, str]] | None,
    list[contracts.Diagnostic],
]:
    if not isinstance(request, HostRegistrationVerificationRequestV1):
        return None, [
            _diagnostic(
                "host-verification.request-invalid",
                "",
                "verification request must use HostRegistrationVerificationRequestV1",
            )
        ]
    diagnostics: list[contracts.Diagnostic] = []
    target = request.target
    if not isinstance(target, host_cmake_target.HostCMakeTargetEvidence):
        diagnostics.append(
            _diagnostic(
                "host-verification.target-invalid",
                "/target",
                "verification requires final Host CMake target evidence",
            )
        )
        artifact_path = None
        build_root = None
    else:
        artifact_path = target.artifact_path
        build_root = target.build_root
        if target.target_type != "EXECUTABLE":
            diagnostics.append(
                _diagnostic(
                    "host-verification.target-invalid",
                    "/target/type",
                    "verification target must be an EXECUTABLE",
                )
            )
        try:
            build_root = build_root.resolve(strict=True)
            artifact_path = artifact_path.resolve(strict=True)
            artifact_path.relative_to(build_root)
            status = artifact_path.lstat()
            if _is_link_or_reparse(status) or not stat.S_ISREG(status.st_mode):
                raise OSError
        except (OSError, ValueError):
            diagnostics.append(
                _diagnostic(
                    "host-verification.artifact-invalid",
                    "/target/artifact",
                    "exact Host artifact must be one regular file inside build root",
                )
            )

    environment, environment_diagnostics = _environment(request.environment)
    diagnostics.extend(environment_diagnostics)
    if (
        not isinstance(request.timeout_seconds, (int, float))
        or isinstance(request.timeout_seconds, bool)
        or not math.isfinite(request.timeout_seconds)
        or request.timeout_seconds <= 0
        or request.timeout_seconds > _MAX_TIMEOUT_SECONDS
    ):
        diagnostics.append(
            _diagnostic(
                "host-verification.timeout-invalid",
                "/timeoutSeconds",
                f"timeout must be positive and at most {_MAX_TIMEOUT_SECONDS:g} seconds",
            )
        )
    if (
        not isinstance(request.max_snapshot_bytes, int)
        or isinstance(request.max_snapshot_bytes, bool)
        or request.max_snapshot_bytes <= 0
        or request.max_snapshot_bytes > DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES
    ):
        diagnostics.append(
            _diagnostic(
                "host-verification.snapshot-limit-invalid",
                "/maxSnapshotBytes",
                "snapshot byte limit must be positive and no larger than the v1 maximum",
            )
        )
    if diagnostics:
        return None, diagnostics
    assert isinstance(artifact_path, Path)
    assert isinstance(build_root, Path)
    assert environment is not None
    return (artifact_path, build_root, environment), []


def run_host_registration_verification(
    request: Any,
    validators: contracts.ContractValidators,
) -> HostRegistrationVerificationOutcomeV1:
    """Execute only the exact Host registration recorder and parse stdout."""

    normalized, diagnostics = _validate_request(request)
    if diagnostics:
        return _failure(diagnostics)
    assert normalized is not None
    assert isinstance(request, HostRegistrationVerificationRequestV1)
    artifact_path, build_root, environment = normalized
    arguments = (str(artifact_path), HOST_REGISTRATION_VERIFICATION_ARGUMENT)
    try:
        completed = subprocess.run(
            arguments,
            cwd=build_root,
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=request.timeout_seconds,
            check=False,
            shell=False,
        )
    except subprocess.TimeoutExpired:
        return _failure(
            [
                _diagnostic(
                    "host-verification.timeout",
                    "/process",
                    "restricted Host verification exceeded its explicit timeout",
                )
            ]
        )
    except OSError:
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
        exit_code=completed.returncode,
        stdout_size=len(completed.stdout),
        stderr_size=len(completed.stderr),
    )
    if completed.returncode != 0:
        return _failure(
            [
                _diagnostic(
                    "host-verification.process-failed",
                    "/process",
                    f"restricted Host returned exit code {completed.returncode}",
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
    if len(completed.stdout) > request.max_snapshot_bytes:
        return _failure(
            [
                _diagnostic(
                    "host-verification.snapshot-too-large",
                    "/process/stdout",
                    "registration snapshot exceeds the explicit v1 byte limit",
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
