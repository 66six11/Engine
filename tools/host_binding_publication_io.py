"""Closed-tree filesystem operations for Host binding publication."""

from __future__ import annotations

import os
import shutil
import stat
from pathlib import Path
from typing import Any


HOST_BINDING_GENERATIONS_DIRECTORY = "generations"


class HostBindingPublicationIoError(Exception):
    def __init__(self, code: str, pointer: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.pointer = pointer
        self.message = message


def _raise(code: str, pointer: str, message: str) -> None:
    raise HostBindingPublicationIoError(code, pointer, message)


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def prepare_publication_generations(path: Any) -> Path:
    if not isinstance(path, Path):
        _raise(
            "host-binding.publication.root-invalid",
            "/publication",
            "publication root must use pathlib.Path",
        )
    absolute = path.absolute()

    def inspect_chain() -> None:
        current = Path(absolute.anchor)
        for component in absolute.parts[1:]:
            current /= component
            try:
                status = current.lstat()
            except FileNotFoundError:
                break
            if _is_link_or_reparse(status) or not stat.S_ISDIR(status.st_mode):
                _raise(
                    "host-binding.publication.root-invalid",
                    "/publication",
                    "publication root must not cross link or reparse components",
                )

    try:
        inspect_chain()
        absolute.mkdir(parents=True, exist_ok=True)
        inspect_chain()
        root = absolute.resolve(strict=True)
        generations = root / HOST_BINDING_GENERATIONS_DIRECTORY
        generations.mkdir(exist_ok=True)
        status = generations.lstat()
        if _is_link_or_reparse(status) or not stat.S_ISDIR(status.st_mode):
            raise OSError
        return generations.resolve(strict=True)
    except HostBindingPublicationIoError:
        raise
    except OSError as error:
        _raise(
            "host-binding.publication.root-invalid",
            "/publication",
            f"could not prepare publication root: {error}",
        )


def write_exact(
    path: Path,
    content: bytes,
    pointer: str,
    *,
    create_parent: bool,
) -> None:
    try:
        if create_parent:
            path.parent.mkdir(parents=True, exist_ok=False)
        with path.open("xb") as stream:
            stream.write(content)
            stream.flush()
        with path.open("rb") as stream:
            actual = stream.read(len(content) + 1)
    except OSError as error:
        _raise(
            "host-binding.publication.write-failed",
            pointer,
            f"could not write staged evidence: {error}",
        )
    if actual != content:
        _raise(
            "host-binding.publication.write-mismatch",
            pointer,
            "staged evidence changed after write",
        )


def cleanup_staging(path: Path | None) -> HostBindingPublicationIoError | None:
    if path is None or not os.path.lexists(path):
        return None
    try:
        shutil.rmtree(path)
        return None
    except OSError as error:
        return HostBindingPublicationIoError(
            "host-binding.publication.cleanup-failed",
            "/publication",
            f"could not remove incomplete staging tree: {error}",
        )


__all__ = [
    "HOST_BINDING_GENERATIONS_DIRECTORY",
    "HostBindingPublicationIoError",
    "cleanup_staging",
    "prepare_publication_generations",
    "write_exact",
]
