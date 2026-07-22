"""Normalize stat fields that must compare across path and open-handle APIs."""

from __future__ import annotations

import os
import stat
from pathlib import Path


def file_kind(status: os.stat_result) -> int:
    """Return only the portable file-kind bits from one stat observation."""

    return stat.S_IFMT(status.st_mode)


def changed_ns(status: os.stat_result) -> int:
    """Return a stable identity timestamp for path/handle comparisons.

    Windows path stat and descriptor fstat can report different meanings for
    ``st_ctime_ns``: the path observation may expose creation time while the
    handle observation exposes the last metadata/write change. Python 3.12+
    exposes their common creation-time value explicitly as ``st_birthtime_ns``.
    Content drift remains covered by inode, size, mtime, and byte hashing.
    """

    if os.name == "nt":
        return getattr(status, "st_birthtime_ns", status.st_ctime_ns)
    return status.st_ctime_ns


def extended_path(path: Path) -> Path:
    """Return a Windows extended-length absolute path for filesystem IO."""

    if os.name != "nt":
        return path.absolute()
    # Extended namespaces do not perform Win32 ``.``/``..`` normalization.
    # Normalize lexically before adding the prefix; abspath does not resolve
    # links, so callers retain responsibility for component-wise reparse checks.
    absolute = Path(os.path.abspath(path))
    value = str(absolute)
    if value.startswith("\\\\?\\"):
        return absolute
    if value.startswith("\\\\"):
        return Path("\\\\?\\UNC\\" + value[2:])
    return Path("\\\\?\\" + value)


def standard_path(path: Path) -> Path:
    """Remove a Windows extended prefix for public/logical path handoffs."""

    if os.name != "nt":
        return path
    value = str(path)
    if value[:8].casefold() == "\\\\?\\unc\\":
        return Path("\\\\" + value[8:])
    if value.startswith("\\\\?\\"):
        return Path(value[4:])
    return path
