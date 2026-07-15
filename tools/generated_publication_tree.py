"""Verify one generated publication against an exact in-memory file set."""

from __future__ import annotations

import os
import stat
from pathlib import Path
from typing import Mapping


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def verify_exact_publication_tree(
    root: Path,
    expected: Mapping[str, bytes],
) -> None:
    """Require one closed, non-link tree whose files match ``expected`` exactly."""

    status = root.lstat()
    if _is_link_or_reparse(status) or not stat.S_ISDIR(status.st_mode):
        raise OSError(f"generation path is not a regular directory: '{root}'")

    expected_directories: set[str] = set()
    for relative in expected:
        parent = Path(relative).parent
        while parent != Path("."):
            expected_directories.add(parent.as_posix())
            parent = parent.parent

    actual_files: set[str] = set()
    actual_directories: set[str] = set()

    def visit(directory: Path) -> None:
        with os.scandir(directory) as iterator:
            entries = sorted(iterator, key=lambda value: os.fsencode(value.name))
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            entry_status = entry.stat(follow_symlinks=False)
            if _is_link_or_reparse(entry_status):
                raise OSError(
                    f"generation contains a link/reparse point: '{relative}'"
                )
            if stat.S_ISREG(entry_status.st_mode):
                actual_files.add(relative)
                if path.read_bytes() != expected.get(relative):
                    raise OSError(f"generation file bytes differ: '{relative}'")
            elif stat.S_ISDIR(entry_status.st_mode):
                actual_directories.add(relative)
                visit(path)
            else:
                raise OSError(
                    f"generation contains a non-regular entry: '{relative}'"
                )

    visit(root)
    if actual_files != set(expected):
        raise OSError("generation file set differs from expected outputs")
    if actual_directories != expected_directories:
        raise OSError("generation directory set differs from expected layout")
