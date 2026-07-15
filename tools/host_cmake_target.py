"""Public query and exact target binding API for generated Hosts."""

from __future__ import annotations

import stat
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_cmake_query as query
from tools import host_cmake_reply as reply


HOST_BUILD_FILE_API_CLIENT = query.HOST_BUILD_FILE_API_CLIENT
HOST_BUILD_FILE_API_MAJOR = query.HOST_BUILD_FILE_API_MAJOR
HOST_BUILD_FILE_API_MINOR = query.HOST_BUILD_FILE_API_MINOR
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
    """Atomically write the Host client's stateful codemodel 2.6 query."""

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
        ),
        diagnostics=(),
    )


def _name_on_disk(value: Any) -> str:
    if not isinstance(value, str) or unicodedata.normalize("NFC", value) != value:
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-name-on-disk-invalid",
                    "/codemodel/target/nameOnDisk",
                    "executable target must expose one normalized nameOnDisk",
                )
            ]
        )
    path = PurePosixPath(value)
    if (
        not value
        or path.is_absolute()
        or len(path.parts) != 1
        or path.name != value
        or value in {".", ".."}
        or "\\" in value
        or ":" in value
    ):
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-name-on-disk-invalid",
                    "/codemodel/target/nameOnDisk",
                    "executable target must expose one normalized nameOnDisk",
                )
            ]
        )
    return value


def _primary_artifact(target: dict[str, Any], name_on_disk: str) -> str:
    artifacts = target.get("artifacts")
    if not isinstance(artifacts, list):
        artifacts = []
    matches = [
        value
        for artifact in artifacts
        if isinstance(artifact, dict)
        for value in [artifact.get("path")]
        if isinstance(value, str) and PurePosixPath(value).name == name_on_disk
    ]
    if len(matches) != 1:
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-primary-artifact-mismatch",
                    "/codemodel/target/artifacts",
                    f"target must expose exactly one artifact named '{name_on_disk}'",
                )
            ]
        )
    return matches[0]


def _artifact_path(
    build_root: Path,
    value: str,
    *,
    require_artifact: bool,
) -> tuple[str, Path]:
    if (
        not value
        or unicodedata.normalize("NFC", value) != value
        or "\\" in value
    ):
        raise _artifact_path_failure()
    raw_path = Path(value)
    if raw_path.is_absolute():
        candidate = raw_path
    else:
        pure = PurePosixPath(value)
        if any(part in {"", ".", ".."} for part in pure.parts) or ":" in value:
            raise _artifact_path_failure()
        candidate = build_root.joinpath(*pure.parts)
    try:
        resolved = candidate.resolve(strict=False)
        relative = resolved.relative_to(build_root).as_posix()
    except (OSError, ValueError):
        raise _artifact_path_failure() from None
    if not require_artifact:
        return relative, resolved

    try:
        status = candidate.lstat()
    except OSError:
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-artifact-missing",
                    "/artifact",
                    "built primary artifact is missing or unreadable",
                )
            ]
        ) from None
    if query.is_link_or_reparse(status) or not stat.S_ISREG(status.st_mode):
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-artifact-not-regular",
                    "/artifact",
                    "built primary artifact must be a regular non-link file",
                )
            ]
        )
    try:
        resolved = candidate.resolve(strict=True)
        relative = resolved.relative_to(build_root).as_posix()
    except (OSError, ValueError):
        raise _artifact_path_failure(
            "built primary artifact must remain inside the explicit build root"
        ) from None
    return relative, resolved


def _artifact_path_failure(
    message: str = "primary artifact must resolve inside the explicit build root",
) -> reply.ReplyFailure:
    return reply.ReplyFailure(
        [
            query.diagnostic(
                "host-build.cmake-artifact-path-invalid",
                "/codemodel/target/artifacts/path",
                message,
            )
        ]
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
    name_on_disk = _name_on_disk(observed.target.get("nameOnDisk"))
    artifact = _primary_artifact(observed.target, name_on_disk)
    relative, path = _artifact_path(
        observed.build_root,
        artifact,
        require_artifact=require_artifact,
    )
    return HostCMakeTargetEvidence(
        build_root=observed.build_root,
        reply_index_path=observed.reply_index_path,
        configuration=observed.configuration,
        target_name=observed.target_name,
        target_type="EXECUTABLE",
        name_on_disk=name_on_disk,
        artifact_relative_path=relative,
        artifact_path=path,
        codemodel_major=observed.codemodel_major,
        codemodel_minor=observed.codemodel_minor,
    )


def read_host_cmake_target(
    build_root: Any,
    configuration: Any,
    target_name: Any,
    *,
    require_artifact: bool,
) -> HostCMakeTargetResult:
    """Read the latest exact Host target from a caller-quiesced build root."""

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
        return _target_failure(diagnostics)
    assert normalized_root is not None
    assert isinstance(configuration, str)
    assert isinstance(target_name, str)
    try:
        observed = reply.read_stable_reply(
            normalized_root,
            configuration,
            target_name,
        )
        evidence = _bind_target(observed, require_artifact)
    except reply.ReplyFailure as failure:
        return _target_failure(failure.diagnostics)
    return HostCMakeTargetResult(evidence, ())


__all__ = [
    "HOST_BUILD_FILE_API_CLIENT",
    "HOST_BUILD_FILE_API_MAJOR",
    "HOST_BUILD_FILE_API_MINOR",
    "HOST_BUILD_FILE_API_READ_ATTEMPTS",
    "HOST_BUILD_MINIMUM_CMAKE_VERSION",
    "HostCMakeFileApiQueryEvidence",
    "HostCMakeFileApiQueryResult",
    "HostCMakeTargetEvidence",
    "HostCMakeTargetResult",
    "read_host_cmake_target",
    "write_host_cmake_file_api_query",
]
