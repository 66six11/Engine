"""Validate inputs for restricted Host registration verification."""

from __future__ import annotations

import math
import os
import re
import stat
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any

from tools import check_package_contracts as contracts
from tools import host_artifact_collection
from tools import host_cmake_target


HOST_REGISTRATION_VERIFICATION_ARGUMENT = "--asharia-verify-static-registration"
DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES = 4 * 1024 * 1024
MAX_VERIFICATION_TIMEOUT_SECONDS = 300.0
_MANIFEST_PATH = "host-registration-verification"
_GENERATION_ID_PATTERN = re.compile(r"^sha256-[0-9a-f]{64}$")
_SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


@dataclass(frozen=True)
class HostRegistrationVerificationRequestV1:
    """Exact build artifact, expected generation, and process inputs."""

    target: host_cmake_target.HostCMakeTargetEvidence
    expected_generation_id: str
    expected_host_activation_blueprint_sha256: str
    environment: tuple[tuple[str, str], ...] = field(repr=False)
    timeout_seconds: float = 60.0
    max_snapshot_bytes: int = DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES


@dataclass(frozen=True)
class StagedHostRegistrationVerificationRequestV1:
    """Collector-owned executable and controlled process inputs."""

    artifact_root: Path = field(repr=False)
    artifact: host_artifact_collection.CollectedHostArtifact = field(repr=False)
    expected_generation_id: str
    expected_host_activation_blueprint_sha256: str
    environment: tuple[tuple[str, str], ...] = field(repr=False)
    timeout_seconds: float = 60.0
    max_snapshot_bytes: int = DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES


@dataclass(frozen=True)
class ValidatedHostRegistrationVerificationRequestV1:
    request: (
        HostRegistrationVerificationRequestV1
        | StagedHostRegistrationVerificationRequestV1
    )
    artifact_path: Path = field(repr=False)
    artifact_root: Path = field(repr=False)
    environment: dict[str, str] = field(repr=False)


def _diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code=code,
        manifest_path=_MANIFEST_PATH,
        pointer=pointer,
        message=message,
    )


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def _regular_artifact_inside_root(root: Path, artifact: Path) -> tuple[Path, Path]:
    root_absolute = Path(os.path.abspath(root))
    artifact_absolute = Path(os.path.abspath(artifact))
    relative = artifact_absolute.relative_to(root_absolute)
    if not relative.parts or any(part in {"", ".", ".."} for part in relative.parts):
        raise OSError

    current = Path(root_absolute.anchor)
    for component in root_absolute.parts[1:]:
        current /= component
        status = current.lstat()
        if _is_link_or_reparse(status) or not stat.S_ISDIR(status.st_mode):
            raise OSError

    current = root_absolute
    for index, component in enumerate(relative.parts):
        current /= component
        status = current.lstat()
        if _is_link_or_reparse(status):
            raise OSError
        final = index == len(relative.parts) - 1
        if final and not stat.S_ISREG(status.st_mode):
            raise OSError
        if not final and not stat.S_ISDIR(status.st_mode):
            raise OSError
    resolved_root = root_absolute.resolve(strict=True)
    resolved_artifact = current.resolve(strict=True)
    resolved_artifact.relative_to(resolved_root)
    return resolved_artifact, resolved_root


def _collected_staged_path(
    root: Path,
    artifact: host_artifact_collection.CollectedHostArtifact,
) -> Path:
    value = artifact.publication_path
    if not isinstance(value, str) or not value or "\\" in value or ":" in value:
        raise ValueError
    relative = PurePosixPath(value)
    if relative.is_absolute() or any(
        part in {"", ".", ".."} for part in relative.parts
    ):
        raise ValueError
    expected = Path(os.path.abspath(root.joinpath(*relative.parts)))
    actual = Path(os.path.abspath(artifact.staged_path))
    if actual != expected or actual.name != artifact.name_on_disk:
        raise ValueError
    return actual


def _environment(
    value: Any,
) -> tuple[dict[str, str] | None, list[contracts.Diagnostic]]:
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


def validate_host_registration_verification_request(
    request: Any,
) -> tuple[
    ValidatedHostRegistrationVerificationRequestV1 | None,
    list[contracts.Diagnostic],
]:
    request_types = (
        HostRegistrationVerificationRequestV1,
        StagedHostRegistrationVerificationRequestV1,
    )
    if not isinstance(request, request_types):
        return None, [
            _diagnostic(
                "host-verification.request-invalid",
                "",
                "verification request must use one supported v1 request type",
            )
        ]

    diagnostics: list[contracts.Diagnostic] = []
    if isinstance(request, HostRegistrationVerificationRequestV1):
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
            artifact_root = None
        else:
            artifact_path = target.artifact_path
            artifact_root = target.build_root
            if target.target_type != "EXECUTABLE":
                diagnostics.append(
                    _diagnostic(
                        "host-verification.target-invalid",
                        "/target/type",
                        "verification target must be an EXECUTABLE",
                    )
                )
        artifact_code = "host-verification.artifact-invalid"
        artifact_pointer = "/target/artifact"
    else:
        artifact = request.artifact
        artifact_root = request.artifact_root
        if not isinstance(
            artifact, host_artifact_collection.CollectedHostArtifact
        ) or not isinstance(artifact_root, Path):
            diagnostics.append(
                _diagnostic(
                    "host-verification.staged-artifact-invalid",
                    "/artifact",
                    "staged verification requires collector-owned artifact evidence",
                )
            )
            artifact_path = None
        else:
            try:
                artifact_path = _collected_staged_path(artifact_root, artifact)
            except ValueError:
                diagnostics.append(
                    _diagnostic(
                        "host-verification.staged-artifact-invalid",
                        "/artifact",
                        "staged artifact path must match its collector publication path",
                    )
                )
                artifact_path = None
        artifact_code = "host-verification.staged-artifact-invalid"
        artifact_pointer = "/artifact"

    if isinstance(artifact_path, Path) and isinstance(artifact_root, Path):
        try:
            artifact_path, artifact_root = _regular_artifact_inside_root(
                artifact_root, artifact_path
            )
        except (OSError, ValueError):
            diagnostics.append(
                _diagnostic(
                    artifact_code,
                    artifact_pointer,
                    "exact Host artifact must be one regular file inside its explicit root",
                )
            )

    environment, environment_diagnostics = _environment(request.environment)
    diagnostics.extend(environment_diagnostics)
    if (
        not isinstance(request.expected_generation_id, str)
        or _GENERATION_ID_PATTERN.fullmatch(request.expected_generation_id) is None
    ):
        diagnostics.append(
            _diagnostic(
                "host-verification.expected-generation-invalid",
                "/expectedGenerationId",
                "expected generation must be one lowercase SHA-256 generation ID",
            )
        )
    if (
        not isinstance(request.expected_host_activation_blueprint_sha256, str)
        or _SHA256_PATTERN.fullmatch(
            request.expected_host_activation_blueprint_sha256
        )
        is None
    ):
        diagnostics.append(
            _diagnostic(
                "host-verification.expected-blueprint-invalid",
                "/expectedHostActivationBlueprintSha256",
                "expected Host Activation Blueprint digest must be lowercase SHA-256",
            )
        )
    if (
        not isinstance(request.timeout_seconds, (int, float))
        or isinstance(request.timeout_seconds, bool)
        or not math.isfinite(request.timeout_seconds)
        or request.timeout_seconds <= 0
        or request.timeout_seconds > MAX_VERIFICATION_TIMEOUT_SECONDS
    ):
        diagnostics.append(
            _diagnostic(
                "host-verification.timeout-invalid",
                "/timeoutSeconds",
                (
                    "timeout must be positive and at most "
                    f"{MAX_VERIFICATION_TIMEOUT_SECONDS:g} seconds"
                ),
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
    assert isinstance(artifact_root, Path)
    assert environment is not None
    return (
        ValidatedHostRegistrationVerificationRequestV1(
            request,
            artifact_path,
            artifact_root,
            environment,
        ),
        [],
    )


__all__ = [
    "DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES",
    "HOST_REGISTRATION_VERIFICATION_ARGUMENT",
    "HostRegistrationVerificationRequestV1",
    "StagedHostRegistrationVerificationRequestV1",
    "ValidatedHostRegistrationVerificationRequestV1",
    "validate_host_registration_verification_request",
]
