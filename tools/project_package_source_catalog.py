"""Deterministic Project/local package source catalog for first resolution.

The project-owned source index contains only portable embedded roots and
logical local source IDs.  Machine-local roots stay in an explicit caller
mapping and are projected to the existing strict candidate loader only for
the source IDs selected by the index.
"""

from __future__ import annotations

import copy
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable, TypeAlias

from tools import check_package_contracts as contracts
from tools import package_candidate_discovery as discovery
from tools import stable_file_identity
from tools.package_candidates import PackageCandidate


_INDEX_SOURCE_KEY = "project-sources"
_WINDOWS_ABSOLUTE_PATH_PATTERN = re.compile(
    r"(?:[A-Za-z]:[\\/]|\\\\(?:\?\\)?[^\\\s]+[\\/])"
)
_POSIX_ABSOLUTE_PATH_PATTERN = re.compile(
    r"(?:^|[\s'\"(])/(?:[^/\s'\")]+/)*[^/\s'\")]+"
)


@dataclass(frozen=True)
class ProjectEmbeddedPackageSourceCatalogEntry:
    """One project-relative embedded source selected by the index."""

    relative_path: str

    @property
    def source_key(self) -> str:
        return f"project-embedded:{self.relative_path}"

    @property
    def kind(self) -> str:
        return "project-embedded"


@dataclass(frozen=True)
class LocalPackageSourceCatalogEntry:
    """One logical local source selected by the index."""

    source_id: str

    @property
    def source_key(self) -> str:
        return f"local:{self.source_id}"

    @property
    def kind(self) -> str:
        return "local"


ProjectPackageSourceCatalogEntry: TypeAlias = (
    ProjectEmbeddedPackageSourceCatalogEntry | LocalPackageSourceCatalogEntry
)


@dataclass(frozen=True)
class ProjectPackageSourceCatalogSnapshot:
    """Immutable logical index and detached strict candidate evidence."""

    entries: tuple[ProjectPackageSourceCatalogEntry, ...]
    _source_index_bytes: bytes = field(repr=False)
    _candidate_snapshot: tuple[PackageCandidate, ...] = field(repr=False)

    @property
    def source_index(self) -> dict[str, Any]:
        """Return an isolated copy of the normalized source index."""

        value = json.loads(self._source_index_bytes.decode("utf-8"))
        assert isinstance(value, dict)
        return value

    @property
    def candidates(self) -> tuple[PackageCandidate, ...]:
        """Return an isolated copy of the exact captured candidates."""

        return copy.deepcopy(self._candidate_snapshot)


@dataclass(frozen=True)
class ProjectPackageSourceCatalogDiagnostic:
    """Stable source-catalog failure without adapter-local absolute paths."""

    code: str
    message: str
    source_key: str = _INDEX_SOURCE_KEY
    location: str = ""

    def render(self) -> str:
        context = (
            contracts.PROJECT_PACKAGE_SOURCES_NAME
            if self.source_key == _INDEX_SOURCE_KEY
            else self.source_key
        )
        if self.location:
            if self.location.startswith("/"):
                context += self.location
            else:
                context += f"/{self.location}"
        return f"{context}: [{self.code}] {self.message}"


@dataclass(frozen=True)
class ProjectPackageSourceCatalogResult:
    """Atomic result: one complete snapshot or deterministic diagnostics."""

    snapshot: ProjectPackageSourceCatalogSnapshot | None
    diagnostics: tuple[ProjectPackageSourceCatalogDiagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.snapshot is not None and not self.diagnostics


@dataclass(frozen=True)
class _ExpectedSource:
    entry: ProjectPackageSourceCatalogEntry
    source: dict[str, str]
    payload_root: Path

    @property
    def source_key(self) -> str:
        return self.entry.source_key


@dataclass(frozen=True)
class _RootIdentity:
    resolved_path: Path
    device: int
    inode: int
    kind: int
    size: int
    modified_ns: int
    changed_ns: int


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8", errors="surrogatepass")


def _diagnostic_sort_key(
    value: ProjectPackageSourceCatalogDiagnostic,
) -> tuple[bytes, str, str, str]:
    return (
        _utf8_key(value.source_key),
        value.location,
        value.code,
        value.message,
    )


def _ordered_diagnostics(
    values: Iterable[ProjectPackageSourceCatalogDiagnostic],
) -> tuple[ProjectPackageSourceCatalogDiagnostic, ...]:
    unique = {
        (value.source_key, value.location, value.code, value.message): value
        for value in values
    }
    return tuple(sorted(unique.values(), key=_diagnostic_sort_key))


def _failure(
    diagnostics: Iterable[ProjectPackageSourceCatalogDiagnostic],
) -> ProjectPackageSourceCatalogResult:
    return ProjectPackageSourceCatalogResult(
        snapshot=None,
        diagnostics=_ordered_diagnostics(diagnostics),
    )


def _is_link(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction is not None and is_junction())


def _capture_project_root(
    value: object,
) -> tuple[Path | None, tuple[ProjectPackageSourceCatalogDiagnostic, ...]]:
    if not isinstance(value, Path) or not value.is_absolute():
        return None, (
            ProjectPackageSourceCatalogDiagnostic(
                code="project-sources.project-root.invalid",
                location="projectRoot",
                message="project root must be an absolute pathlib.Path directory",
            ),
        )
    try:
        if _is_link(value) or not value.is_dir():
            raise OSError
        resolved = value.resolve(strict=True)
    except (OSError, RuntimeError):
        return None, (
            ProjectPackageSourceCatalogDiagnostic(
                code="project-sources.project-root.unavailable",
                location="projectRoot",
                message="project root must resolve to an available non-link directory",
            ),
        )
    return resolved, ()


def _read_source_index(
    project_root: Path,
    validators: contracts.ContractValidators,
) -> tuple[
    dict[str, Any] | None,
    bytes | None,
    tuple[ProjectPackageSourceCatalogDiagnostic, ...],
]:
    path = project_root / contracts.PROJECT_PACKAGE_SOURCES_NAME
    try:
        if _is_link(path) or not path.is_file():
            raise OSError
        exact_bytes = path.read_bytes()
    except OSError:
        return None, None, (
            ProjectPackageSourceCatalogDiagnostic(
                code="project-sources.index.unavailable",
                message=(
                    f"'{contracts.PROJECT_PACKAGE_SOURCES_NAME}' must be an "
                    "available non-link regular file under the project root"
                ),
            ),
        )

    try:
        parsed = json.loads(exact_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None, exact_bytes, (
            ProjectPackageSourceCatalogDiagnostic(
                code="contract.manifest.json",
                message="project package source index must be valid UTF-8 JSON without a BOM",
            ),
        )
    if type(parsed) is not dict:
        return None, exact_bytes, (
            ProjectPackageSourceCatalogDiagnostic(
                code="contract.manifest.document",
                message="project package source index root must be a JSON object",
            ),
        )

    contract_diagnostics = contracts.validate_manifest_data(
        parsed,
        contracts.PROJECT_PACKAGE_SOURCES_NAME,
        validators,
    )
    if contract_diagnostics:
        return parsed, exact_bytes, _ordered_diagnostics(
            ProjectPackageSourceCatalogDiagnostic(
                code=value.code,
                location=value.pointer,
                message=f"project package source index violates [{value.code}]",
            )
            for value in contract_diagnostics
        )
    return parsed, exact_bytes, ()


def _entry(value: dict[str, Any]) -> ProjectPackageSourceCatalogEntry:
    if value["kind"] == "project-embedded":
        return ProjectEmbeddedPackageSourceCatalogEntry(
            relative_path=value["relativePath"],
        )
    if value["kind"] == "local":
        return LocalPackageSourceCatalogEntry(source_id=value["sourceId"])
    raise ValueError("validated source index contains an unsupported source kind")


def _capture_selected_local_mappings(
    selected_source_ids: set[str],
    local_sources: Iterable[object],
) -> tuple[
    dict[str, discovery.LocalCandidateLocation],
    tuple[ProjectPackageSourceCatalogDiagnostic, ...],
]:
    if not selected_source_ids:
        return {}, ()
    try:
        values = tuple(local_sources)
    except (TypeError, RuntimeError):
        return {}, (
            ProjectPackageSourceCatalogDiagnostic(
                code="project-sources.local-mapping.invalid",
                location="localMappings",
                message="local source mappings must be iterable",
            ),
        )

    selected: dict[str, discovery.LocalCandidateLocation] = {}
    occurrences: dict[str, int] = {}
    diagnostics: list[ProjectPackageSourceCatalogDiagnostic] = []
    for value in values:
        if not isinstance(value, discovery.LocalCandidateLocation):
            continue
        source_id = value.source_id
        if not isinstance(source_id, str) or source_id not in selected_source_ids:
            continue
        occurrences[source_id] = occurrences.get(source_id, 0) + 1
        if not isinstance(value.payload_root, Path) or not value.payload_root.is_absolute():
            diagnostics.append(
                ProjectPackageSourceCatalogDiagnostic(
                    code="project-sources.local-mapping.invalid",
                    source_key=f"local:{source_id}",
                    message=(
                        "selected local source mapping must contain an absolute "
                        "pathlib.Path root"
                    ),
                )
            )
            continue
        selected[source_id] = discovery.LocalCandidateLocation(
            source_id=source_id,
            payload_root=value.payload_root,
        )

    for source_id in sorted(selected_source_ids, key=_utf8_key):
        count = occurrences.get(source_id, 0)
        if count == 0:
            diagnostics.append(
                ProjectPackageSourceCatalogDiagnostic(
                    code="project-sources.local-mapping.missing",
                    source_key=f"local:{source_id}",
                    message="selected local source ID has no explicit machine-local mapping",
                )
            )
        elif count > 1:
            diagnostics.append(
                ProjectPackageSourceCatalogDiagnostic(
                    code="project-sources.local-mapping.duplicate",
                    source_key=f"local:{source_id}",
                    message="selected local source ID is mapped more than once",
                )
            )
    return selected, _ordered_diagnostics(diagnostics)


def _expected_sources(
    project_root: Path,
    entries: tuple[ProjectPackageSourceCatalogEntry, ...],
    local_mappings: dict[str, discovery.LocalCandidateLocation],
) -> tuple[_ExpectedSource, ...]:
    result: list[_ExpectedSource] = []
    for entry in entries:
        if entry.kind == "project-embedded":
            source = {
                "kind": "project-embedded",
                "relativePath": entry.relative_path,
            }
            root = project_root.joinpath(*entry.relative_path.split("/"))
        else:
            source = {"kind": "local", "sourceId": entry.source_id}
            root = local_mappings[entry.source_id].payload_root
        result.append(_ExpectedSource(entry=entry, source=source, payload_root=root))
    return tuple(sorted(result, key=lambda value: _utf8_key(value.source_key)))


def _candidate_locations(
    project_root: Path,
    expected: tuple[_ExpectedSource, ...],
) -> tuple[discovery.CandidateLocation, ...]:
    locations: list[discovery.CandidateLocation] = []
    for value in expected:
        if value.entry.kind == "project-embedded":
            locations.append(
                discovery.ProjectEmbeddedCandidateLocation(
                    project_root=project_root,
                    relative_path=value.entry.relative_path,
                )
            )
        else:
            locations.append(
                discovery.LocalCandidateLocation(
                    source_id=value.entry.source_id,
                    payload_root=value.payload_root,
                )
            )
    return tuple(locations)


def _root_identity(path: Path) -> _RootIdentity | None:
    try:
        resolved = path.resolve(strict=True)
        status = resolved.stat()
    except (OSError, RuntimeError):
        return None
    return _RootIdentity(
        resolved_path=resolved,
        device=status.st_dev,
        inode=status.st_ino,
        kind=stable_file_identity.file_kind(status),
        size=status.st_size,
        modified_ns=status.st_mtime_ns,
        changed_ns=stable_file_identity.changed_ns(status),
    )


def _capture_root_identities(
    expected: tuple[_ExpectedSource, ...],
) -> dict[str, _RootIdentity | None]:
    return {value.source_key: _root_identity(value.payload_root) for value in expected}


def _root_change_diagnostics(
    expected: tuple[_ExpectedSource, ...],
    captured: dict[str, _RootIdentity | None],
) -> tuple[ProjectPackageSourceCatalogDiagnostic, ...]:
    diagnostics = []
    for value in expected:
        if _root_identity(value.payload_root) == captured[value.source_key]:
            continue
        diagnostics.append(
            ProjectPackageSourceCatalogDiagnostic(
                code="project-sources.candidate.root-changed",
                source_key=value.source_key,
                location="root",
                message=(
                    "selected package root changed while candidate evidence "
                    "was collected"
                ),
            )
        )
    return _ordered_diagnostics(diagnostics)


def _contains_absolute_path(message: str, sensitive_roots: Iterable[Path]) -> bool:
    folded = message.casefold()
    for root in sensitive_roots:
        rendered = str(root)
        if rendered and rendered.casefold() in folded:
            return True
    return bool(
        _WINDOWS_ABSOLUTE_PATH_PATTERN.search(message)
        or _POSIX_ABSOLUTE_PATH_PATTERN.search(message)
    )


def _safe_discovery_location(value: object) -> str:
    if not isinstance(value, str) or not value or "\\" in value:
        return ""
    if value.startswith("/") or any(part in {"", ".", ".."} for part in value.split("/")):
        return ""
    if _WINDOWS_ABSOLUTE_PATH_PATTERN.search(value):
        return ""
    return value


def _discovery_diagnostics(
    values: Iterable[discovery.CandidateDiscoveryDiagnostic],
    sensitive_roots: Iterable[Path],
    expected_source_keys: set[str],
) -> tuple[ProjectPackageSourceCatalogDiagnostic, ...]:
    roots = tuple(sensitive_roots)
    diagnostics: list[ProjectPackageSourceCatalogDiagnostic] = []
    for value in values:
        message = value.message if isinstance(value.message, str) else ""
        if not message or _contains_absolute_path(message, roots):
            message = "strict Project/local candidate loading failed"
        else:
            message = f"strict Project/local candidate loading failed: {message}"
        source_key = (
            value.source_key
            if isinstance(value.source_key, str)
            and value.source_key in expected_source_keys
            else _INDEX_SOURCE_KEY
        )
        diagnostics.append(
            ProjectPackageSourceCatalogDiagnostic(
                code=value.code,
                source_key=source_key,
                location=_safe_discovery_location(value.location),
                message=message,
            )
        )
    return _ordered_diagnostics(diagnostics)


def _candidate_binding_diagnostics(
    candidates: tuple[PackageCandidate, ...],
    expected: tuple[_ExpectedSource, ...],
) -> tuple[
    tuple[PackageCandidate, ...],
    tuple[ProjectPackageSourceCatalogDiagnostic, ...],
]:
    expected_by_key = {value.source_key: value for value in expected}
    candidates_by_origin: dict[str, list[PackageCandidate]] = {}
    diagnostics: list[ProjectPackageSourceCatalogDiagnostic] = []
    resolved_roots: dict[str, Path] = {}
    for candidate in candidates:
        origin = candidate.origin if isinstance(candidate.origin, str) else ""
        candidates_by_origin.setdefault(origin, []).append(candidate)
        if origin not in expected_by_key:
            diagnostics.append(
                ProjectPackageSourceCatalogDiagnostic(
                    code="project-sources.candidate.unexpected",
                    message="strict loader returned a candidate absent from the source index",
                )
            )

    ordered_candidates: list[PackageCandidate] = []
    for value in expected:
        matches = candidates_by_origin.get(value.source_key, [])
        if len(matches) != 1:
            diagnostics.append(
                ProjectPackageSourceCatalogDiagnostic(
                    code=(
                        "project-sources.candidate.missing"
                        if not matches
                        else "project-sources.candidate.duplicate"
                    ),
                    source_key=value.source_key,
                    message="each source-index entry must produce exactly one candidate",
                )
            )
            continue
        candidate = matches[0]
        if candidate.source != value.source:
            diagnostics.append(
                ProjectPackageSourceCatalogDiagnostic(
                    code="project-sources.candidate.source-mismatch",
                    source_key=value.source_key,
                    message="loaded candidate source differs from the source-index entry",
                    location="source",
                )
            )
        try:
            if not isinstance(candidate.payload_location, Path):
                raise ValueError
            resolved_candidate_root = candidate.payload_location.resolve(strict=True)
            if resolved_candidate_root != value.payload_root.resolve(strict=True):
                raise ValueError
            resolved_roots[value.source_key] = resolved_candidate_root
        except (OSError, RuntimeError, ValueError):
            diagnostics.append(
                ProjectPackageSourceCatalogDiagnostic(
                    code="project-sources.candidate.root-mismatch",
                    source_key=value.source_key,
                    message="loaded candidate root differs from the selected source mapping",
                    location="root",
                )
            )
        ordered_candidates.append(candidate)

    ordered_source_keys = sorted(resolved_roots, key=_utf8_key)
    for left_index, left_key in enumerate(ordered_source_keys):
        left_root = resolved_roots[left_key]
        for right_key in ordered_source_keys[left_index + 1 :]:
            right_root = resolved_roots[right_key]
            if left_root not in right_root.parents and right_root not in left_root.parents:
                continue
            diagnostics.append(
                ProjectPackageSourceCatalogDiagnostic(
                    code="project-sources.candidate.overlapping-root",
                    source_key=left_key,
                    message=(
                        f"source roots '{left_key}' and '{right_key}' must not "
                        "contain one another"
                    ),
                )
            )

    return tuple(ordered_candidates), _ordered_diagnostics(diagnostics)


def _source_index_changed(project_root: Path, expected_bytes: bytes) -> bool:
    path = project_root / contracts.PROJECT_PACKAGE_SOURCES_NAME
    try:
        return _is_link(path) or not path.is_file() or path.read_bytes() != expected_bytes
    except OSError:
        return True


def derive_project_package_source_catalog(
    project_root: object,
    local_sources: Iterable[object],
    validators: contracts.ContractValidators,
) -> ProjectPackageSourceCatalogResult:
    """Derive one atomic Project/local candidate snapshot without a Lock."""

    captured_root, root_diagnostics = _capture_project_root(project_root)
    if captured_root is None:
        return _failure(root_diagnostics)

    source_index, exact_bytes, index_diagnostics = _read_source_index(
        captured_root,
        validators,
    )
    if source_index is None or exact_bytes is None or index_diagnostics:
        return _failure(index_diagnostics)

    entries = tuple(
        sorted(
            (_entry(value) for value in source_index["sources"]),
            key=lambda value: _utf8_key(value.source_key),
        )
    )
    selected_local_ids = {
        value.source_id for value in entries if value.kind == "local"
    }
    local_mappings, mapping_diagnostics = _capture_selected_local_mappings(
        selected_local_ids,
        local_sources,
    )
    if mapping_diagnostics:
        return _failure(mapping_diagnostics)

    expected = _expected_sources(captured_root, entries, local_mappings)
    captured_root_identities = _capture_root_identities(expected)
    loaded = discovery.load_package_candidates(
        _candidate_locations(captured_root, expected),
        validators,
    )
    if not loaded.succeeded:
        return _failure(
            _discovery_diagnostics(
                loaded.diagnostics,
                (captured_root, *(value.payload_root for value in expected)),
                {value.source_key for value in expected},
            )
        )

    ordered_candidates, binding_diagnostics = _candidate_binding_diagnostics(
        loaded.candidates,
        expected,
    )
    if binding_diagnostics:
        return _failure(binding_diagnostics)
    final_diagnostics: list[ProjectPackageSourceCatalogDiagnostic] = []
    if _source_index_changed(captured_root, exact_bytes):
        final_diagnostics.append(
            ProjectPackageSourceCatalogDiagnostic(
                code="project-sources.index.changed",
                message=(
                    "project package source index changed while candidate "
                    "evidence was collected"
                ),
            )
        )
    final_diagnostics.extend(
        _root_change_diagnostics(expected, captured_root_identities)
    )
    if final_diagnostics:
        return _failure(final_diagnostics)

    normalized_bytes = contracts.render_normalized_project_package_sources(
        source_index
    ).encode("utf-8")
    return ProjectPackageSourceCatalogResult(
        snapshot=ProjectPackageSourceCatalogSnapshot(
            entries=entries,
            _source_index_bytes=normalized_bytes,
            _candidate_snapshot=copy.deepcopy(ordered_candidates),
        ),
        diagnostics=(),
    )


__all__ = [
    "LocalPackageSourceCatalogEntry",
    "ProjectEmbeddedPackageSourceCatalogEntry",
    "ProjectPackageSourceCatalogDiagnostic",
    "ProjectPackageSourceCatalogEntry",
    "ProjectPackageSourceCatalogResult",
    "ProjectPackageSourceCatalogSnapshot",
    "derive_project_package_source_catalog",
]
