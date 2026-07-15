"""Run one explicit final Host configure/build and bind its CMake target."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_build_request
from tools import host_cmake_target
from tools.host_build_request import FinalHostBuildRequestV1


_MANIFEST_PATH = "final-host-build"


@dataclass(frozen=True)
class HostBuildProcessEvidence:
    """Stable process facts without treating log text as build state."""

    stage: str
    arguments: tuple[str, ...]
    exit_code: int


@dataclass(frozen=True)
class FinalHostBuildOutcomeV1:
    """Atomic build outcome; target evidence exists only after full success."""

    target: host_cmake_target.HostCMakeTargetEvidence | None
    query: host_cmake_target.HostCMakeFileApiQueryEvidence | None
    configure: HostBuildProcessEvidence | None
    build: HostBuildProcessEvidence | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.target is not None and not self.diagnostics


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
    *,
    query: host_cmake_target.HostCMakeFileApiQueryEvidence | None = None,
    configure: HostBuildProcessEvidence | None = None,
    build: HostBuildProcessEvidence | None = None,
) -> FinalHostBuildOutcomeV1:
    return FinalHostBuildOutcomeV1(
        target=None,
        query=query,
        configure=configure,
        build=build,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _execute(
    stage: str,
    arguments: tuple[str, ...],
    source_root: Path,
    environment: dict[str, str],
    timeout_seconds: float,
) -> tuple[HostBuildProcessEvidence | None, contracts.Diagnostic | None]:
    try:
        completed = subprocess.run(
            arguments,
            cwd=source_root,
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_seconds,
            check=False,
            shell=False,
        )
    except subprocess.TimeoutExpired:
        return None, _diagnostic(
            f"host-build.{stage}-timeout",
            f"/{stage}",
            f"{stage} process exceeded its explicit timeout",
        )
    except OSError:
        return None, _diagnostic(
            f"host-build.{stage}-spawn-failed",
            f"/{stage}",
            f"could not start the explicit {stage} process",
        )
    evidence = HostBuildProcessEvidence(stage, arguments, completed.returncode)
    if completed.returncode != 0:
        return evidence, _diagnostic(
            f"host-build.{stage}-failed",
            f"/{stage}",
            f"{stage} process returned exit code {completed.returncode}",
        )
    return evidence, None


def run_final_host_build(
    request: Any,
    validators: contracts.ContractValidators,
) -> FinalHostBuildOutcomeV1:
    """Configure and build exactly one final Host target with no shell parsing."""

    validated, diagnostics = host_build_request.validate_final_host_build_request(
        request, validators
    )
    if diagnostics:
        return _failure(diagnostics)
    assert validated is not None
    assert isinstance(request, FinalHostBuildRequestV1)

    query_result = host_cmake_target.write_host_cmake_file_api_query(
        validated.build_root
    )
    if not query_result.succeeded:
        return _failure(query_result.diagnostics)
    assert query_result.evidence is not None
    query = query_result.evidence

    configure_arguments = host_build_request.configure_final_host_arguments(
        request, validated, query.build_root
    )
    configure, configure_diagnostic = _execute(
        "configure",
        configure_arguments,
        validated.source_root,
        validated.environment,
        request.configure_timeout_seconds,
    )
    if configure_diagnostic is not None:
        return _failure(
            [configure_diagnostic],
            query=query,
            configure=configure,
        )

    configured_target = host_cmake_target.read_host_cmake_target(
        query.build_root,
        request.configuration,
        request.target_name,
        require_artifact=False,
    )
    if not configured_target.succeeded:
        return _failure(
            configured_target.diagnostics,
            query=query,
            configure=configure,
        )

    build_arguments = host_build_request.build_final_host_arguments(
        request, validated, query.build_root
    )
    build, build_diagnostic = _execute(
        "build",
        build_arguments,
        validated.source_root,
        validated.environment,
        request.build_timeout_seconds,
    )
    if build_diagnostic is not None:
        return _failure(
            [build_diagnostic],
            query=query,
            configure=configure,
            build=build,
        )

    built_target = host_cmake_target.read_host_cmake_target(
        query.build_root,
        request.configuration,
        request.target_name,
        require_artifact=True,
    )
    if not built_target.succeeded:
        return _failure(
            built_target.diagnostics,
            query=query,
            configure=configure,
            build=build,
        )
    assert built_target.evidence is not None
    return FinalHostBuildOutcomeV1(
        target=built_target.evidence,
        query=query,
        configure=configure,
        build=build,
        diagnostics=(),
    )
