"""Write the fixed stateful CMake File API query for generated Hosts."""

from __future__ import annotations

import json
import os
import stat
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from tools import check_package_contracts as contracts


HOST_BUILD_FILE_API_CLIENT = "client-asharia-host-build-v1"
HOST_BUILD_FILE_API_MAJOR = 2
HOST_BUILD_FILE_API_MINOR = 6
HOST_BUILD_FILE_API_READ_ATTEMPTS = 3
HOST_BUILD_MINIMUM_CMAKE_VERSION = (3, 28)

FILE_API_ROOT = Path(".cmake/api/v1")
QUERY_RELATIVE_PATH = (
    FILE_API_ROOT / "query" / HOST_BUILD_FILE_API_CLIENT / "query.json"
)
REPLY_RELATIVE_PATH = FILE_API_ROOT / "reply"
MANIFEST_PATH = "host-cmake-target"


@dataclass(frozen=True)
class QueryWriteEvidence:
    build_root: Path
    query_path: Path


class QueryWriteFailure(Exception):
    def __init__(self, diagnostic: contracts.Diagnostic) -> None:
        super().__init__()
        self.diagnostic = diagnostic


def diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code=code,
        manifest_path=MANIFEST_PATH,
        pointer=pointer,
        message=message,
    )


def diagnostic_sort_key(
    value: contracts.Diagnostic,
) -> tuple[str, str, str, str]:
    return (value.manifest_path, value.pointer, value.code, value.message)


def is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def explicit_build_root(value: Any, *, create: bool) -> Path | None:
    if not isinstance(value, Path):
        return None
    absolute = value.absolute()
    try:
        if create:
            absolute.mkdir(parents=True, exist_ok=True)
        status = absolute.lstat()
        if is_link_or_reparse(status) or not stat.S_ISDIR(status.st_mode):
            return None
        return absolute.resolve(strict=True)
    except OSError:
        return None


def _query_bytes() -> bytes:
    return (
        json.dumps(
            {
                "requests": [
                    {
                        "kind": "codemodel",
                        "version": {
                            "major": HOST_BUILD_FILE_API_MAJOR,
                            "minor": HOST_BUILD_FILE_API_MINOR,
                        },
                    }
                ]
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n"
    ).encode("utf-8")


def write_query(build_root: Any) -> QueryWriteEvidence:
    normalized_root = explicit_build_root(build_root, create=True)
    if normalized_root is None:
        raise QueryWriteFailure(
            diagnostic(
                "host-build.cmake-build-root-invalid",
                "/buildRoot",
                "build root must be an explicit regular directory Path",
            )
        )

    query_path = normalized_root / QUERY_RELATIVE_PATH
    temporary_path: Path | None = None
    try:
        query_path.parent.mkdir(parents=True, exist_ok=True)
        query_parent = query_path.parent.resolve(strict=True)
        query_parent.relative_to(normalized_root)
        with tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=".query-",
            suffix=".json.tmp",
            dir=query_parent,
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write(_query_bytes())
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_path, query_path)
        temporary_path = None
    except (OSError, ValueError):
        raise QueryWriteFailure(
            diagnostic(
                "host-build.cmake-query-write-failed",
                "/query",
                "could not atomically write the stateful CMake File API query",
            )
        ) from None
    finally:
        if temporary_path is not None:
            try:
                temporary_path.unlink(missing_ok=True)
            except OSError:
                pass
    return QueryWriteEvidence(normalized_root, query_path)
