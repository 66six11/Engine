"""Repository-only Python payload policy for formal product inventories."""

from __future__ import annotations

import posixpath
import re
from dataclasses import dataclass
from typing import Iterable


_PYTHON_PAYLOAD_EXTENSIONS = frozenset(
    {
        ".egg",
        ".pth",
        ".py",
        ".pyc",
        ".pyd",
        ".pyi",
        ".pyo",
        ".pyw",
        ".pyz",
        ".whl",
    }
)
_PYTHON_PAYLOAD_SEGMENTS = frozenset(
    {
        ".venv",
        "__pycache__",
        "dist-packages",
        "graalpy",
        "ironpython",
        "jython",
        "pypy",
        "pypy3",
        "pythonnet",
        "site-packages",
        "venv",
        "virtualenv",
    }
)
_PYTHON_RUNTIME_DIRECTORY_PATTERN = re.compile(
    r"^python(?:(?:[0-9]+(?:\.[0-9]+)*)t?)?(?:_d)?$",
    re.IGNORECASE,
)
_PYTHON_RUNTIME_FILE_PATTERN = re.compile(
    r"^python[a-z0-9._-]*(?:\.exe|\.dll|\.lib|\.zip|\._pth|\.pth)$",
    re.IGNORECASE,
)
_PIP_RUNTIME_FILE_PATTERN = re.compile(
    r"^pip(?:[0-9]+(?:\.[0-9]+)*)?(?:\.exe|\.dll|\.zip|\._pth|\.pth)$",
    re.IGNORECASE,
)
_LIBPYTHON_RUNTIME_FILE_PATTERN = re.compile(
    r"^libpython[a-z0-9._-]*\.(?:a|dll|dylib|lib|so)(?:\.[0-9]+)*$",
    re.IGNORECASE,
)
_MANAGED_PYTHON_RUNTIME_FILE_PATTERN = re.compile(
    r"^(?:python\.runtime|ironpython)(?:[.-][a-z0-9_-]+)*\.dll$",
    re.IGNORECASE,
)
_ALTERNATIVE_PYTHON_RUNTIME_FILE_PATTERN = re.compile(
    r"^(?:ipyw?|pypy(?:3)?|jython|graalpy)[a-z0-9._-]*\.(?:exe|dll|jar|zip)$",
    re.IGNORECASE,
)
_LIBPYPY_RUNTIME_FILE_PATTERN = re.compile(
    r"^libpypy(?:3)?[a-z0-9._-]*\.(?:a|dll|dylib|lib|so)(?:\.[0-9]+)*$",
    re.IGNORECASE,
)
_EXACT_RUNTIME_FILE_NAMES = frozenset(
    {
        "py.exe",
        "pyw.exe",
        "pymanager.exe",
        "pywmanager.exe",
        "pyvenv.cfg",
    }
)


@dataclass(frozen=True, order=True)
class ForbiddenPythonProductPayload:
    """One stable logical-path policy match."""

    path: str
    reason: str


def match_forbidden_python_product_payload(
    path: str,
) -> ForbiddenPythonProductPayload | None:
    """Return a policy match for one already-normalized logical product path."""

    if not isinstance(path, str) or not path:
        return None
    segments = path.split("/")
    folded_segments = tuple(segment.casefold() for segment in segments)
    for segment, folded in zip(segments, folded_segments, strict=True):
        if (
            folded in _PYTHON_PAYLOAD_SEGMENTS
            or folded.endswith(".dist-info")
            or folded.endswith(".egg-info")
            or _PYTHON_RUNTIME_DIRECTORY_PATTERN.fullmatch(segment) is not None
        ):
            return ForbiddenPythonProductPayload(
                path,
                f"Python package or virtual-environment segment '{segment}'",
            )

    file_name = segments[-1]
    folded_file_name = folded_segments[-1]
    extension = posixpath.splitext(folded_file_name)[1]
    if extension in _PYTHON_PAYLOAD_EXTENSIONS:
        return ForbiddenPythonProductPayload(
            path,
            f"Python payload extension '{extension}'",
        )
    if (
        folded_file_name in _EXACT_RUNTIME_FILE_NAMES
        or _PYTHON_RUNTIME_FILE_PATTERN.fullmatch(file_name) is not None
        or _PIP_RUNTIME_FILE_PATTERN.fullmatch(file_name) is not None
        or _LIBPYTHON_RUNTIME_FILE_PATTERN.fullmatch(file_name) is not None
        or _MANAGED_PYTHON_RUNTIME_FILE_PATTERN.fullmatch(file_name) is not None
        or _ALTERNATIVE_PYTHON_RUNTIME_FILE_PATTERN.fullmatch(file_name) is not None
        or _LIBPYPY_RUNTIME_FILE_PATTERN.fullmatch(file_name) is not None
    ):
        return ForbiddenPythonProductPayload(
            path,
            f"Python interpreter or runtime artifact '{file_name}'",
        )
    return None


def find_forbidden_python_product_payloads(
    paths: Iterable[str],
) -> tuple[ForbiddenPythonProductPayload, ...]:
    """Return deterministic, de-duplicated matches for logical inventory paths."""

    matches: dict[str, ForbiddenPythonProductPayload] = {}
    for path in paths:
        match = match_forbidden_python_product_payload(path)
        if match is not None:
            matches.setdefault(match.path, match)
    return tuple(
        sorted(
            matches.values(),
            key=lambda value: (value.path.encode("utf-8"), value.reason),
        )
    )
