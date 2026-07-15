"""Collect one final Host executable into a caller-owned staging root."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_artifact_io
from tools import host_cmake_target
from tools import static_composition_root as composition
from tools.host_executable_binding import HOST_EXECUTABLE_MEDIA_TYPE


HOST_ARTIFACT_DIRECTORY = "host"
HOST_EXECUTABLE_ROLE = "host-executable"
HOST_ARTIFACT_COPY_CHUNK_SIZE = (
    host_artifact_io.HOST_ARTIFACT_COPY_CHUNK_SIZE
)

# Keep the observation API on this module while sharing its low-level I/O.
_FileFingerprint = host_artifact_io.FileFingerprint
HostArtifactObservation = host_artifact_io.HostArtifactObservation
HostArtifactObservationResult = host_artifact_io.HostArtifactObservationResult
_CollectionFailure = host_artifact_io.ArtifactIOFailure
_stream_observation = host_artifact_io.stream_observation
_copy_to_staging = host_artifact_io.copy_to_staging


@dataclass(frozen=True)
class CollectedHostArtifact:
    """Exact source bytes copied into an owned staging generation."""

    source_path: Path = field(repr=False)
    staged_path: Path = field(repr=False)
    publication_path: str
    name_on_disk: str
    role: str
    media_type: str
    size: int
    integrity: composition.IntegrityRecord
    source_fingerprint: _FileFingerprint = field(repr=False)


@dataclass(frozen=True)
class HostArtifactCollectionResult:
    artifact: CollectedHostArtifact | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.artifact is not None and not self.diagnostics


def _diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return host_artifact_io.diagnostic(code, pointer, message)


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostArtifactCollectionResult:
    return HostArtifactCollectionResult(
        None,
        tuple(sorted(diagnostics, key=host_artifact_io.diagnostic_sort_key)),
    )


def _raise(code: str, pointer: str, message: str) -> None:
    host_artifact_io.raise_failure(code, pointer, message)


def observe_host_artifact(path: Any) -> HostArtifactObservationResult:
    """Stream and hash one stable regular file without retaining its bytes."""

    return host_artifact_io.observe_host_artifact(path)


def _validated_roots(
    target: Any, staging_root: Any
) -> tuple[host_cmake_target.HostCMakeTargetEvidence, Path, Path]:
    if not isinstance(target, host_cmake_target.HostCMakeTargetEvidence):
        _raise(
            "host-binding.artifact.target-invalid",
            "/target",
            "collection requires final Host CMake target evidence",
        )
    if target.target_type != "EXECUTABLE":
        _raise(
            "host-binding.artifact.target-invalid",
            "/target/type",
            "collection target must be an EXECUTABLE",
        )
    if not isinstance(staging_root, Path):
        _raise(
            "host-binding.artifact.staging-invalid",
            "/publication",
            "staging root must use pathlib.Path",
        )
    build_root = host_artifact_io.existing_directory_without_links(
        target.build_root, "/target/buildRoot"
    )
    staging = host_artifact_io.existing_directory_without_links(
        staging_root, "/publication"
    )
    if (
        PurePosixPath(target.artifact_relative_path).name
        != target.name_on_disk
        or target.artifact_path.name != target.name_on_disk
    ):
        _raise(
            "host-binding.artifact.name-mismatch",
            "/target/nameOnDisk",
            "target nameOnDisk must identify the exact source artifact",
        )
    relative = host_artifact_io.artifact_relative_path(
        target.artifact_relative_path, target.name_on_disk
    )
    source = host_artifact_io.source_from_binding(build_root, relative)
    try:
        target_source = target.artifact_path.resolve(strict=True)
        target_source.relative_to(build_root)
    except (OSError, ValueError):
        _raise(
            "host-binding.artifact.path-invalid",
            "/artifact",
            "target artifact must resolve inside the exact build root",
        )
    if target_source != source:
        _raise(
            "host-binding.artifact.binding-mismatch",
            "/target/artifactRelativePath",
            "target artifact path disagrees with its File API relative binding",
        )
    if host_artifact_io.paths_overlap(build_root, staging):
        _raise(
            "host-binding.artifact.root-overlap",
            "/publication",
            "owned staging and mutable build roots must not overlap",
        )
    if source.name != target.name_on_disk:
        _raise(
            "host-binding.artifact.name-mismatch",
            "/target/nameOnDisk",
            "target nameOnDisk must identify the exact source artifact",
        )
    host_artifact_io.regular_status(source, "/artifact")
    return target, source, staging


def collect_host_artifact(
    target: Any, staging_root: Any
) -> HostArtifactCollectionResult:
    """Copy, independently re-hash, and return one staged executable."""

    destination: Path | None = None
    parent_existed = True
    try:
        target, source, staging = _validated_roots(target, staging_root)
        publication_path = f"{HOST_ARTIFACT_DIRECTORY}/{target.name_on_disk}"
        destination = staging.joinpath(*publication_path.split("/"))
        parent_existed = os.path.lexists(destination.parent)
        source_observation = _copy_to_staging(source, destination)
        staged_observation = _stream_observation(destination, "/artifact")
        if (
            staged_observation.size != source_observation.size
            or staged_observation.integrity != source_observation.integrity
        ):
            _raise(
                "host-binding.artifact.staged-hash-mismatch",
                "/artifact",
                "staged Host artifact differs from the collected source bytes",
            )
        return HostArtifactCollectionResult(
            CollectedHostArtifact(
                source,
                destination,
                publication_path,
                target.name_on_disk,
                HOST_EXECUTABLE_ROLE,
                HOST_EXECUTABLE_MEDIA_TYPE,
                staged_observation.size,
                staged_observation.integrity,
                source_observation.fingerprint,
            ),
            (),
        )
    except _CollectionFailure as error:
        diagnostics = [error.diagnostic]
        if destination is not None and not parent_existed:
            try:
                if os.path.lexists(destination):
                    destination.unlink()
                destination.parent.rmdir()
            except OSError as cleanup_error:
                diagnostics.append(
                    _diagnostic(
                        "host-binding.artifact.cleanup-failed",
                        "/publication",
                        f"could not clean partial staged artifact: {cleanup_error}",
                    )
                )
        return _failure(diagnostics)


def verify_collected_host_artifact(
    artifact: Any,
    *,
    verify_source: bool,
) -> tuple[contracts.Diagnostic, ...]:
    """Re-hash staged bytes and optionally prove the source stayed quiescent."""

    if not isinstance(artifact, CollectedHostArtifact) or not isinstance(
        verify_source, bool
    ):
        return (
            _diagnostic(
                "host-binding.artifact.evidence-invalid",
                "/artifact",
                "verification requires collected Host artifact evidence",
            ),
        )
    try:
        staged = _stream_observation(artifact.staged_path, "/artifact")
        if staged.size != artifact.size or staged.integrity != artifact.integrity:
            _raise(
                "host-binding.artifact.staged-hash-mismatch",
                "/artifact",
                "staged Host artifact changed after collection",
            )
        if verify_source:
            source = _stream_observation(artifact.source_path, "/artifact")
            if (
                source.fingerprint != artifact.source_fingerprint
                or source.size != artifact.size
                or source.integrity != artifact.integrity
            ):
                _raise(
                    "host-binding.artifact.source-drift",
                    "/artifact",
                    "source Host artifact changed after collection",
                )
        return ()
    except _CollectionFailure as error:
        return (error.diagnostic,)


__all__ = [
    "HOST_ARTIFACT_COPY_CHUNK_SIZE",
    "HOST_ARTIFACT_DIRECTORY",
    "HOST_EXECUTABLE_MEDIA_TYPE",
    "HOST_EXECUTABLE_ROLE",
    "CollectedHostArtifact",
    "HostArtifactCollectionResult",
    "HostArtifactObservation",
    "HostArtifactObservationResult",
    "collect_host_artifact",
    "observe_host_artifact",
    "verify_collected_host_artifact",
]
