"""Stable file and path evidence for Host executable collection."""

from __future__ import annotations

import hashlib
import os
import stat
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any

from tools import check_package_contracts as contracts
from tools import stable_file_identity
from tools import static_composition_root as composition


HOST_ARTIFACT_COPY_CHUNK_SIZE = 1024 * 1024
_MANIFEST_PATH = "asharia.host-executable-binding-receipt.json"


@dataclass(frozen=True)
class FileFingerprint:
    device: int
    inode: int
    mode: int
    size: int
    modified_ns: int
    changed_ns: int
    file_attributes: int


@dataclass(frozen=True)
class HostArtifactObservation:
    """One stable regular-file observation."""

    size: int
    integrity: composition.IntegrityRecord
    fingerprint: FileFingerprint = field(repr=False)


@dataclass(frozen=True)
class HostArtifactObservationResult:
    observation: HostArtifactObservation | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.observation is not None and not self.diagnostics


class ArtifactIOFailure(Exception):
    def __init__(self, diagnostic: contracts.Diagnostic) -> None:
        super().__init__(diagnostic.message)
        self.diagnostic = diagnostic


def diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(code, _MANIFEST_PATH, pointer, message)


def diagnostic_sort_key(
    item: contracts.Diagnostic,
) -> tuple[str, str, str, str]:
    return (item.manifest_path, item.pointer, item.code, item.message)


def raise_failure(code: str, pointer: str, message: str) -> None:
    raise ArtifactIOFailure(diagnostic(code, pointer, message))


def _fingerprint(status: os.stat_result) -> FileFingerprint:
    return FileFingerprint(
        status.st_dev,
        status.st_ino,
        stable_file_identity.file_kind(status),
        status.st_size,
        status.st_mtime_ns,
        stable_file_identity.changed_ns(status),
        getattr(status, "st_file_attributes", 0),
    )


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def regular_status(path: Path, pointer: str) -> os.stat_result:
    try:
        status = path.lstat()
    except OSError as error:
        raise_failure(
            "host-binding.artifact.file-invalid",
            pointer,
            f"could not inspect Host artifact '{path}': {error}",
        )
    if _is_link_or_reparse(status) or not stat.S_ISREG(status.st_mode):
        raise_failure(
            "host-binding.artifact.file-invalid",
            pointer,
            "Host artifact must be one regular non-link file",
        )
    return status


def stream_observation(path: Path, pointer: str) -> HostArtifactObservation:
    initial = _fingerprint(regular_status(path, pointer))
    digest = hashlib.sha256()
    size = 0
    try:
        with path.open("rb") as stream:
            opened = _fingerprint(os.fstat(stream.fileno()))
            if opened != initial:
                raise_failure(
                    "host-binding.artifact.changed-during-read",
                    pointer,
                    "Host artifact changed while it was opened",
                )
            while chunk := stream.read(HOST_ARTIFACT_COPY_CHUNK_SIZE):
                digest.update(chunk)
                size += len(chunk)
            if _fingerprint(os.fstat(stream.fileno())) != opened:
                raise_failure(
                    "host-binding.artifact.changed-during-read",
                    pointer,
                    "Host artifact changed while it was read",
                )
    except ArtifactIOFailure:
        raise
    except OSError as error:
        raise_failure(
            "host-binding.artifact.read-failed",
            pointer,
            f"could not read Host artifact '{path}': {error}",
        )
    if _fingerprint(regular_status(path, pointer)) != initial:
        raise_failure(
            "host-binding.artifact.changed-during-read",
            pointer,
            "Host artifact changed after it was read",
        )
    return HostArtifactObservation(
        size,
        composition.IntegrityRecord("sha256", digest.hexdigest()),
        initial,
    )


def observe_host_artifact(path: Any) -> HostArtifactObservationResult:
    """Stream and hash one stable regular file without retaining its bytes."""

    if not isinstance(path, Path):
        return HostArtifactObservationResult(
            None,
            (
                diagnostic(
                    "host-binding.artifact.path-invalid",
                    "/artifact",
                    "Host artifact path must use pathlib.Path",
                ),
            ),
        )
    try:
        return HostArtifactObservationResult(
            stream_observation(path, "/artifact"), ()
        )
    except ArtifactIOFailure as error:
        return HostArtifactObservationResult(None, (error.diagnostic,))


def paths_overlap(left: Path, right: Path) -> bool:
    try:
        left.relative_to(right)
        return True
    except ValueError:
        pass
    try:
        right.relative_to(left)
        return True
    except ValueError:
        return False


def existing_directory_without_links(path: Path, pointer: str) -> Path:
    absolute = path.absolute()
    current = Path(absolute.anchor)
    try:
        for component in absolute.parts[1:]:
            current /= component
            status = current.lstat()
            if _is_link_or_reparse(status) or not stat.S_ISDIR(status.st_mode):
                raise OSError
        return absolute.resolve(strict=True)
    except OSError:
        raise_failure(
            "host-binding.artifact.path-invalid",
            pointer,
            "artifact roots must not cross link or reparse components",
        )


def artifact_relative_path(value: str, name_on_disk: str) -> PurePosixPath:
    if (
        not value
        or unicodedata.normalize("NFC", value) != value
        or "\\" in value
        or "//" in value
        or value.endswith("/")
        or ":" in value
    ):
        raise_failure(
            "host-binding.artifact.path-invalid",
            "/target/artifactRelativePath",
            "File API artifact path must be one normalized relative path",
        )
    path = PurePosixPath(value)
    if (
        path.is_absolute()
        or any(part in {"", ".", ".."} for part in path.parts)
        or path.name != name_on_disk
    ):
        raise_failure(
            "host-binding.artifact.path-invalid",
            "/target/artifactRelativePath",
            "File API artifact path must end in the exact nameOnDisk",
        )
    return path


def source_from_binding(build_root: Path, relative: PurePosixPath) -> Path:
    current = build_root
    try:
        for index, component in enumerate(relative.parts):
            current /= component
            status = current.lstat()
            if _is_link_or_reparse(status):
                raise OSError
            is_final = index == len(relative.parts) - 1
            if is_final and not stat.S_ISREG(status.st_mode):
                raise_failure(
                    "host-binding.artifact.file-invalid",
                    "/artifact",
                    "Host artifact must be one regular non-link file",
                )
            if not is_final and not stat.S_ISDIR(status.st_mode):
                raise OSError
        return current.resolve(strict=True)
    except OSError:
        raise_failure(
            "host-binding.artifact.path-invalid",
            "/target/artifactRelativePath",
            "File API artifact path must traverse regular non-link entries",
        )


def copy_to_staging(source: Path, destination: Path) -> HostArtifactObservation:
    initial = _fingerprint(regular_status(source, "/artifact"))
    destination.parent.mkdir(parents=True, exist_ok=False)
    digest = hashlib.sha256()
    size = 0
    try:
        with source.open("rb") as source_stream, destination.open(
            "xb"
        ) as destination_stream:
            opened = _fingerprint(os.fstat(source_stream.fileno()))
            if opened != initial:
                raise_failure(
                    "host-binding.artifact.changed-during-copy",
                    "/artifact",
                    "source Host artifact changed while it was opened",
                )
            while chunk := source_stream.read(HOST_ARTIFACT_COPY_CHUNK_SIZE):
                destination_stream.write(chunk)
                digest.update(chunk)
                size += len(chunk)
            destination_stream.flush()
            if _fingerprint(os.fstat(source_stream.fileno())) != opened:
                raise_failure(
                    "host-binding.artifact.changed-during-copy",
                    "/artifact",
                    "source Host artifact changed while it was copied",
                )
    except ArtifactIOFailure:
        raise
    except OSError as error:
        raise_failure(
            "host-binding.artifact.copy-failed",
            "/publication",
            f"could not stage exact Host artifact bytes: {error}",
        )
    if _fingerprint(regular_status(source, "/artifact")) != initial:
        raise_failure(
            "host-binding.artifact.changed-during-copy",
            "/artifact",
            "source Host artifact changed after it was copied",
        )
    return HostArtifactObservation(
        size,
        composition.IntegrityRecord("sha256", digest.hexdigest()),
        initial,
    )


__all__ = [
    "HOST_ARTIFACT_COPY_CHUNK_SIZE",
    "ArtifactIOFailure",
    "FileFingerprint",
    "HostArtifactObservation",
    "HostArtifactObservationResult",
    "artifact_relative_path",
    "copy_to_staging",
    "diagnostic",
    "diagnostic_sort_key",
    "existing_directory_without_links",
    "observe_host_artifact",
    "paths_overlap",
    "raise_failure",
    "regular_status",
    "source_from_binding",
    "stream_observation",
]
