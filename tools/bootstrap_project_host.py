"""Bounded Project Bootstrap Host process protocol for project open."""

from __future__ import annotations

import json
import math
import uuid
from pathlib import Path
from typing import Any, Iterable

from tools import bootstrap_current_host
from tools import bootstrap_session as bootstrap
from tools import host_process


PROJECT_BOOTSTRAP_PROJECT_ROOT_ARGUMENT = "--asharia-project-root"
PROJECT_BOOTSTRAP_PROJECT_REJECTED_EXIT_CODE = 65
PROJECT_BOOTSTRAP_SUMMARY_SCHEMA = "com.asharia.project-bootstrap-summary"
PROJECT_BOOTSTRAP_SUMMARY_SCHEMA_VERSION = 1
DEFAULT_PROJECT_BOOTSTRAP_TIMEOUT_SECONDS = 60.0
MAX_PROJECT_BOOTSTRAP_TIMEOUT_SECONDS = 300.0
DEFAULT_MAX_PROJECT_BOOTSTRAP_SUMMARY_BYTES = 64 * 1024


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


def _run_observation(
    disposition: bootstrap.ProjectBootstrapDispositionV1,
    diagnostics: Iterable[bootstrap.BootstrapSessionDiagnostic],
    *,
    summary: bootstrap.ProjectBootstrapSummaryV1 | None = None,
    exit_code: int | None = None,
    stdout_size: int = 0,
    stderr_size: int = 0,
) -> bootstrap.ProjectBootstrapRunObservationV1:
    return bootstrap.ProjectBootstrapRunObservationV1(
        disposition,
        summary,
        exit_code,
        stdout_size,
        stderr_size,
        _ordered_diagnostics(diagnostics),
    )


def _host_failure(
    code: str,
    pointer: str,
    message: str,
    *,
    exit_code: int | None = None,
    stdout_size: int = 0,
    stderr_size: int = 0,
) -> bootstrap.ProjectBootstrapRunObservationV1:
    return _run_observation(
        bootstrap.ProjectBootstrapDispositionV1.HOST_FAILED,
        (bootstrap.diagnostic(code, pointer, message),),
        exit_code=exit_code,
        stdout_size=stdout_size,
        stderr_size=stderr_size,
    )


def _environment(
    value: Any,
) -> tuple[dict[str, str] | None, bootstrap.BootstrapSessionDiagnostic | None]:
    if not isinstance(value, tuple):
        return None, bootstrap.diagnostic(
            "bootstrap.host.environment-invalid",
            "/projectBootstrap/environment",
            "Host environment must be an explicit tuple of key/value pairs",
        )
    result: dict[str, str] = {}
    folded_keys: set[str] = set()
    for item in value:
        if (
            not isinstance(item, tuple)
            or len(item) != 2
            or not isinstance(item[0], str)
            or not isinstance(item[1], str)
            or not item[0]
            or "=" in item[0]
            or "\0" in item[0]
            or "\0" in item[1]
            or item[0].casefold() in folded_keys
        ):
            return None, bootstrap.diagnostic(
                "bootstrap.host.environment-invalid",
                "/projectBootstrap/environment",
                "Host environment entries must be unique valid string pairs",
            )
        folded_keys.add(item[0].casefold())
        result[item[0]] = item[1]
    return result, None


def _timeout_is_valid(value: object) -> bool:
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
        and value > 0
        and value <= MAX_PROJECT_BOOTSTRAP_TIMEOUT_SECONDS
    )


def _output_limits_are_valid(summary: object, stderr: object) -> bool:
    return (
        isinstance(summary, int)
        and not isinstance(summary, bool)
        and 0 < summary <= DEFAULT_MAX_PROJECT_BOOTSTRAP_SUMMARY_BYTES
        and isinstance(stderr, int)
        and not isinstance(stderr, bool)
        and 0 < stderr <= host_process.MAX_HOST_STDERR_BYTES
    )


def is_valid_project_bootstrap_process_policy(
    environment: object,
    timeout_seconds: object,
    max_summary_bytes: object,
    max_stderr_bytes: object,
) -> bool:
    """Validate bounded process policy without launching or inspecting a project."""

    normalized_environment, _ = _environment(environment)
    return (
        normalized_environment is not None
        and _timeout_is_valid(timeout_seconds)
        and _output_limits_are_valid(max_summary_bytes, max_stderr_bytes)
    )


class _DuplicateSummaryKey(ValueError):
    pass


def _strict_json_object(content: bytes) -> dict[str, Any] | None:
    def object_from_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise _DuplicateSummaryKey
            result[key] = value
        return result

    def reject_constant(_: str) -> None:
        raise ValueError

    try:
        value = json.loads(
            content.decode("utf-8"),
            object_pairs_hook=object_from_pairs,
            parse_constant=reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError):
        return None
    return value if isinstance(value, dict) else None


def _is_nonempty_unicode_scalar_string(value: object) -> bool:
    return (
        isinstance(value, str)
        and bool(value)
        and all(not 0xD800 <= ord(character) <= 0xDFFF for character in value)
    )


def _parse_project_bootstrap_summary(
    content: bytes,
) -> bootstrap.ProjectBootstrapSummaryV1 | None:
    data = _strict_json_object(content)
    if data is None or set(data) != {
        "schema",
        "schemaVersion",
        "projectName",
        "projectId",
        "assetSourceRootCount",
    }:
        return None
    if (
        data["schema"] != PROJECT_BOOTSTRAP_SUMMARY_SCHEMA
        or not isinstance(data["schemaVersion"], int)
        or isinstance(data["schemaVersion"], bool)
        or data["schemaVersion"] != PROJECT_BOOTSTRAP_SUMMARY_SCHEMA_VERSION
        or not _is_nonempty_unicode_scalar_string(data["projectName"])
        or not isinstance(data["projectId"], str)
        or not isinstance(data["assetSourceRootCount"], int)
        or isinstance(data["assetSourceRootCount"], bool)
        or data["assetSourceRootCount"] < 0
        or data["assetSourceRootCount"] > 2**64 - 1
    ):
        return None
    try:
        project_id = uuid.UUID(data["projectId"])
    except (ValueError, AttributeError):
        return None
    if project_id.int == 0 or str(project_id) != data["projectId"]:
        return None
    return bootstrap.ProjectBootstrapSummaryV1(
        data["projectName"],
        data["projectId"],
        data["assetSourceRootCount"],
    )


def run_project_bootstrap_host(
    project_root: Any,
    current_image: Any,
    environment: Any,
    *,
    timeout_seconds: float = DEFAULT_PROJECT_BOOTSTRAP_TIMEOUT_SECONDS,
    max_summary_bytes: int = DEFAULT_MAX_PROJECT_BOOTSTRAP_SUMMARY_BYTES,
    max_stderr_bytes: int = host_process.MAX_HOST_STDERR_BYTES,
) -> bootstrap.ProjectBootstrapRunObservationV1:
    """Run the exact published Host with fixed argv and bounded output."""

    if (
        not isinstance(current_image, bootstrap.CurrentImageObservationV1)
        or current_image.disposition
        is not bootstrap.CurrentImageDispositionV1.CURRENT
        or not isinstance(
            current_image.payload, bootstrap_current_host.CurrentHostPayloadV1
        )
    ):
        return _host_failure(
            "bootstrap.host.current-image-required",
            "/projectBootstrap/currentImage",
            "Project Bootstrap execution requires one admitted current Host image",
        )
    artifact_path = bootstrap_current_host.current_host_artifact_for_execution(
        current_image.payload
    )
    if artifact_path is None:
        return _host_failure(
            "bootstrap.host.current-image-invalid",
            "/projectBootstrap/currentImage",
            "admitted Host payload no longer matches its published binding artifact",
        )
    if not isinstance(project_root, Path):
        return _host_failure(
            "bootstrap.host.project-root-invalid",
            "/projectBootstrap/projectRoot",
            "Project Bootstrap execution requires one canonical pathlib.Path root",
        )
    try:
        canonical_project_root = project_root.resolve(strict=True)
    except OSError:
        return _host_failure(
            "bootstrap.host.project-root-invalid",
            "/projectBootstrap/projectRoot",
            "the inspected project root no longer exists",
        )
    if not canonical_project_root.is_dir():
        return _host_failure(
            "bootstrap.host.project-root-invalid",
            "/projectBootstrap/projectRoot",
            "the inspected project root is not a directory",
        )

    process_environment, environment_error = _environment(environment)
    if environment_error is not None:
        return _run_observation(
            bootstrap.ProjectBootstrapDispositionV1.HOST_FAILED,
            (environment_error,),
        )
    if not _timeout_is_valid(timeout_seconds):
        return _host_failure(
            "bootstrap.host.timeout-invalid",
            "/projectBootstrap/timeoutSeconds",
            "Host timeout must be positive and no greater than 300 seconds",
        )
    if not _output_limits_are_valid(max_summary_bytes, max_stderr_bytes):
        return _host_failure(
            "bootstrap.host.output-limit-invalid",
            "/projectBootstrap/outputLimits",
            "Host output limits must be positive and within the v1 maxima",
        )
    assert process_environment is not None

    arguments = (
        str(artifact_path),
        PROJECT_BOOTSTRAP_PROJECT_ROOT_ARGUMENT,
        str(canonical_project_root),
    )
    try:
        completed = host_process.run_bounded_host_process(
            arguments,
            process_environment,
            float(timeout_seconds),
            max_summary_bytes,
            max_stderr_bytes,
        )
    except host_process.HostProcessTimeout:
        return _host_failure(
            "bootstrap.host.timeout",
            "/projectBootstrap/process",
            "Project Bootstrap Host exceeded its explicit timeout",
        )
    except (host_process.HostProcessSpawnFailure, OSError):
        return _host_failure(
            "bootstrap.host.spawn-failed",
            "/projectBootstrap/process",
            "could not start the exact published project Host",
        )

    sizes = {
        "exit_code": completed.return_code,
        "stdout_size": completed.stdout_size,
        "stderr_size": completed.stderr_size,
    }
    if completed.limit_exceeded is not None:
        return _host_failure(
            "bootstrap.host.output-too-large",
            f"/projectBootstrap/process/{completed.limit_exceeded}",
            "Project Bootstrap Host output exceeded its explicit byte limit",
            **sizes,
        )
    if (
        completed.return_code == PROJECT_BOOTSTRAP_PROJECT_REJECTED_EXIT_CODE
        and not completed.stdout
    ):
        return _run_observation(
            bootstrap.ProjectBootstrapDispositionV1.PROJECT_REJECTED,
            (
                bootstrap.diagnostic(
                    "bootstrap.host.project-rejected",
                    "/projectBootstrap/project",
                    "Project Bootstrap rejected the project descriptor",
                ),
            ),
            **sizes,
        )
    if completed.return_code != 0:
        return _host_failure(
            "bootstrap.host.process-failed",
            "/projectBootstrap/process",
            f"Project Bootstrap Host returned exit code {completed.return_code}",
            **sizes,
        )
    if completed.stderr:
        return _host_failure(
            "bootstrap.host.unexpected-stderr",
            "/projectBootstrap/process/stderr",
            "successful Project Bootstrap execution must not write stderr",
            **sizes,
        )
    summary = _parse_project_bootstrap_summary(completed.stdout)
    if summary is None:
        return _host_failure(
            "bootstrap.host.summary-invalid",
            "/projectBootstrap/summary",
            "Project Bootstrap stdout is not one strict v1 summary",
            **sizes,
        )
    return _run_observation(
        bootstrap.ProjectBootstrapDispositionV1.SUCCEEDED,
        (),
        summary=summary,
        **sizes,
    )


__all__ = [
    "DEFAULT_MAX_PROJECT_BOOTSTRAP_SUMMARY_BYTES",
    "DEFAULT_PROJECT_BOOTSTRAP_TIMEOUT_SECONDS",
    "MAX_PROJECT_BOOTSTRAP_TIMEOUT_SECONDS",
    "PROJECT_BOOTSTRAP_PROJECT_REJECTED_EXIT_CODE",
    "PROJECT_BOOTSTRAP_PROJECT_ROOT_ARGUMENT",
    "PROJECT_BOOTSTRAP_SUMMARY_SCHEMA",
    "PROJECT_BOOTSTRAP_SUMMARY_SCHEMA_VERSION",
    "is_valid_project_bootstrap_process_policy",
    "run_project_bootstrap_host",
]
