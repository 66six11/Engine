"""Public query and exact target binding API for generated Hosts."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_cmake_artifact
from tools import host_cmake_query as query
from tools import host_cmake_reply as reply


HOST_BUILD_FILE_API_CLIENT = query.HOST_BUILD_FILE_API_CLIENT
HOST_BUILD_FILE_API_MAJOR = query.HOST_BUILD_FILE_API_MAJOR
HOST_BUILD_FILE_API_MINOR = query.HOST_BUILD_FILE_API_MINOR
HOST_BUILD_TOOLCHAINS_FILE_API_MAJOR = query.HOST_BUILD_TOOLCHAINS_FILE_API_MAJOR
HOST_BUILD_TOOLCHAINS_FILE_API_MINOR = query.HOST_BUILD_TOOLCHAINS_FILE_API_MINOR
HOST_BUILD_FILE_API_READ_ATTEMPTS = query.HOST_BUILD_FILE_API_READ_ATTEMPTS
HOST_BUILD_MINIMUM_CMAKE_VERSION = query.HOST_BUILD_MINIMUM_CMAKE_VERSION


@dataclass(frozen=True)
class HostCMakeFileApiQueryEvidence:
    """The exact stateful query written before final CMake configure."""

    build_root: Path = field(repr=False)
    query_path: Path = field(repr=False)
    client_name: str
    codemodel_major: int
    codemodel_minor: int
    toolchains_major: int = HOST_BUILD_TOOLCHAINS_FILE_API_MAJOR
    toolchains_minor: int = HOST_BUILD_TOOLCHAINS_FILE_API_MINOR


@dataclass(frozen=True)
class HostCMakeFileApiQueryResult:
    """Atomic query-write result."""

    evidence: HostCMakeFileApiQueryEvidence | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.evidence is not None and not self.diagnostics


@dataclass(frozen=True)
class HostCMakeTargetEvidence:
    """Final configure binding without artifact-byte or receipt claims."""

    build_root: Path = field(repr=False)
    reply_index_path: Path = field(repr=False)
    configuration: str
    target_name: str
    target_type: str
    name_on_disk: str
    artifact_relative_path: str
    artifact_path: Path = field(repr=False)
    codemodel_major: int
    codemodel_minor: int


@dataclass(frozen=True)
class HostCMakeTargetResult:
    """Atomic target-read result."""

    evidence: HostCMakeTargetEvidence | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.evidence is not None and not self.diagnostics


def _sorted_diagnostics(
    diagnostics: Iterable[contracts.Diagnostic],
) -> tuple[contracts.Diagnostic, ...]:
    return tuple(sorted(diagnostics, key=query.diagnostic_sort_key))


def _query_failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostCMakeFileApiQueryResult:
    return HostCMakeFileApiQueryResult(None, _sorted_diagnostics(diagnostics))


def _target_failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostCMakeTargetResult:
    return HostCMakeTargetResult(None, _sorted_diagnostics(diagnostics))


def write_host_cmake_file_api_query(
    build_root: Any,
) -> HostCMakeFileApiQueryResult:
    """Write the Host client's codemodel 2.6 and toolchains 1.0 query."""

    try:
        written = query.write_query(build_root)
    except query.QueryWriteFailure as failure:
        return _query_failure([failure.diagnostic])
    return HostCMakeFileApiQueryResult(
        evidence=HostCMakeFileApiQueryEvidence(
            build_root=written.build_root,
            query_path=written.query_path,
            client_name=HOST_BUILD_FILE_API_CLIENT,
            codemodel_major=HOST_BUILD_FILE_API_MAJOR,
            codemodel_minor=HOST_BUILD_FILE_API_MINOR,
            toolchains_major=HOST_BUILD_TOOLCHAINS_FILE_API_MAJOR,
            toolchains_minor=HOST_BUILD_TOOLCHAINS_FILE_API_MINOR,
        ),
        diagnostics=(),
    )


def _bind_target(
    observed: reply.HostCMakeReplyEvidence,
    require_artifact: bool,
) -> HostCMakeTargetEvidence:
    if observed.target.get("type") != "EXECUTABLE":
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-target-type-mismatch",
                    "/codemodel/target/type",
                    f"target '{observed.target_name}' must be an EXECUTABLE",
                )
            ]
        )
    artifact = host_cmake_artifact.bind_primary_host_artifact(
        observed.target,
        observed.build_root,
        require_artifact=require_artifact,
    )
    return HostCMakeTargetEvidence(
        build_root=observed.build_root,
        reply_index_path=observed.reply_index_path,
        configuration=observed.configuration,
        target_name=observed.target_name,
        target_type="EXECUTABLE",
        name_on_disk=artifact.name_on_disk,
        artifact_relative_path=artifact.relative_path,
        artifact_path=artifact.path,
        codemodel_major=observed.codemodel_major,
        codemodel_minor=observed.codemodel_minor,
    )


def _validated_target_request(
    build_root: Any,
    configuration: Any,
    target_name: Any,
    *,
    require_artifact: bool,
) -> tuple[Path, str, str, bool]:
    diagnostics: list[contracts.Diagnostic] = []
    normalized_root = query.explicit_build_root(build_root, create=False)
    if normalized_root is None:
        diagnostics.append(
            query.diagnostic(
                "host-build.cmake-build-root-invalid",
                "/buildRoot",
                "build root must be an explicit regular directory Path",
            )
        )
    if not isinstance(configuration, str) or not configuration:
        diagnostics.append(
            query.diagnostic(
                "host-build.cmake-configuration-invalid",
                "/configuration",
                "configuration must be a non-empty string",
            )
        )
    if not isinstance(target_name, str) or not target_name:
        diagnostics.append(
            query.diagnostic(
                "host-build.cmake-target-invalid",
                "/target",
                "target name must be a non-empty string",
            )
        )
    if not isinstance(require_artifact, bool):
        diagnostics.append(
            query.diagnostic(
                "host-build.cmake-artifact-requirement-invalid",
                "/requireArtifact",
                "require_artifact must be a boolean",
            )
        )
    if diagnostics:
        raise reply.ReplyFailure(diagnostics)
    assert normalized_root is not None
    assert isinstance(configuration, str)
    assert isinstance(target_name, str)
    return normalized_root, configuration, target_name, require_artifact


def read_host_cmake_target(
    build_root: Any,
    configuration: Any,
    target_name: Any,
    *,
    require_artifact: bool,
) -> HostCMakeTargetResult:
    """Read the latest exact Host target from a caller-quiesced build root."""

    try:
        (
            normalized_root,
            normalized_configuration,
            normalized_target_name,
            normalized_requirement,
        ) = _validated_target_request(
            build_root,
            configuration,
            target_name,
            require_artifact=require_artifact,
        )
        observed = reply.read_stable_reply(
            normalized_root,
            normalized_configuration,
            normalized_target_name,
        )
        evidence = _bind_target(observed, normalized_requirement)
    except reply.ReplyFailure as failure:
        return _target_failure(failure.diagnostics)
    return HostCMakeTargetResult(evidence, ())


__all__ = [
    "HOST_BUILD_FILE_API_CLIENT",
    "HOST_BUILD_FILE_API_MAJOR",
    "HOST_BUILD_FILE_API_MINOR",
    "HOST_BUILD_FILE_API_READ_ATTEMPTS",
    "HOST_BUILD_TOOLCHAINS_FILE_API_MAJOR",
    "HOST_BUILD_TOOLCHAINS_FILE_API_MINOR",
    "HOST_BUILD_MINIMUM_CMAKE_VERSION",
    "HostCMakeFileApiQueryEvidence",
    "HostCMakeFileApiQueryResult",
    "HostCMakeTargetEvidence",
    "HostCMakeTargetResult",
    "read_host_cmake_target",
    "write_host_cmake_file_api_query",
]
