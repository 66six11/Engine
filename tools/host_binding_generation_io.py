"""Stable, link-safe reads for immutable Host binding generation trees."""

from __future__ import annotations

import os
import stat
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from tools import host_artifact_io


class HostBindingGenerationIOError(Exception):
    """One closed-tree IO or layout violation with verifier diagnostics."""

    def __init__(self, code: str, pointer: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.pointer = pointer
        self.message = message


@dataclass(frozen=True)
class HostBindingGenerationClosingObservation:
    receipt_bytes: bytes
    snapshot_bytes: bytes
    artifact: host_artifact_io.HostArtifactObservationResult


def _raise(code: str, pointer: str, message: str) -> None:
    raise HostBindingGenerationIOError(code, pointer, message)


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def _same_file_observation(
    left: os.stat_result,
    right: os.stat_result,
) -> bool:
    def fingerprint(value: os.stat_result) -> tuple[int, ...]:
        return (
            value.st_dev,
            value.st_ino,
            stat.S_IFMT(value.st_mode),
            value.st_size,
            value.st_mtime_ns,
            value.st_ctime_ns,
            getattr(value, "st_file_attributes", 0),
        )

    return fingerprint(left) == fingerprint(right)


def resolve_existing_generation_root(path: Path) -> Path:
    """Resolve a directory only when every existing component is non-link."""

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
        _raise(
            "host-binding.publication.path-invalid",
            "/publication",
            "generation path must not cross link or reparse components",
        )


def read_stable_small_regular(path: Path, limit: int, pointer: str) -> bytes:
    """Read one bounded regular file while rejecting observation drift."""

    try:
        status = path.lstat()
        if (
            _is_link_or_reparse(status)
            or not stat.S_ISREG(status.st_mode)
            or status.st_size <= 0
            or status.st_size > limit
        ):
            raise OSError
        with path.open("rb") as stream:
            opened = os.fstat(stream.fileno())
            if not _same_file_observation(status, opened):
                raise OSError
            content = stream.read(limit + 1)
            finished = os.fstat(stream.fileno())
        if (
            len(content) != status.st_size
            or len(content) > limit
            or not _same_file_observation(opened, finished)
            or not _same_file_observation(status, path.lstat())
        ):
            raise OSError
        return content
    except OSError:
        _raise(
            "host-binding.publication.evidence-read-failed",
            pointer,
            "published binding evidence is missing, unstable, or oversized",
        )


def validate_closed_generation_layout(
    root: Path,
    expected_files: Iterable[str],
    expected_directories: Iterable[str],
) -> None:
    """Require a closed tree of regular files and directories only."""

    actual_files: set[str] = set()
    actual_directories: set[str] = set()
    try:
        for path in root.rglob("*"):
            relative = path.relative_to(root).as_posix()
            status = path.lstat()
            if _is_link_or_reparse(status):
                raise OSError
            if stat.S_ISREG(status.st_mode):
                actual_files.add(relative)
            elif stat.S_ISDIR(status.st_mode):
                actual_directories.add(relative)
            else:
                raise OSError
    except (OSError, ValueError):
        _raise(
            "host-binding.publication.layout-invalid",
            "/publication",
            "binding generation contains an unreadable or non-regular entry",
        )
    if (
        actual_files != set(expected_files)
        or actual_directories != set(expected_directories)
    ):
        _raise(
            "host-binding.publication.layout-mismatch",
            "/publication",
            "binding generation must contain only its receipt, Host, and snapshot",
        )


def close_generation_observation(
    root: Path,
    *,
    receipt_path: Path,
    receipt_limit: int,
    snapshot_path: Path,
    snapshot_limit: int,
    artifact_path: Path,
    expected_files: Iterable[str],
    expected_directories: Iterable[str],
) -> HostBindingGenerationClosingObservation:
    """Re-observe all evidence before accepting the cooperative immutable tree."""

    receipt_bytes = read_stable_small_regular(
        receipt_path,
        receipt_limit,
        "/receipt",
    )
    snapshot_bytes = read_stable_small_regular(
        snapshot_path,
        snapshot_limit,
        "/registrationSnapshot",
    )
    artifact = host_artifact_io.observe_host_artifact(artifact_path)
    validate_closed_generation_layout(
        root,
        expected_files,
        expected_directories,
    )
    return HostBindingGenerationClosingObservation(
        receipt_bytes,
        snapshot_bytes,
        artifact,
    )


__all__ = [
    "HostBindingGenerationIOError",
    "HostBindingGenerationClosingObservation",
    "close_generation_observation",
    "read_stable_small_regular",
    "resolve_existing_generation_root",
    "validate_closed_generation_layout",
]
