#!/usr/bin/env python3
"""Compare v1 source-boundary target dependencies with CMake's configured graph."""

from __future__ import annotations

import argparse
import json
import os
import stat
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


MANIFEST_NAME = "asharia.package.json"
IGNORED_PATH_PARTS = frozenset({".git", "build", "generated"})
MINIMUM_CODEMODEL_MINOR = 9
DIRECT_RELATION_FIELDS = (
    "linkLibraries",
    "interfaceLinkLibraries",
    "compileDependencies",
    "interfaceCompileDependencies",
    "objectDependencies",
    "orderDependencies",
)
FRAGMENT_RELATION_FIELDS = frozenset(
    {"linkLibraries", "interfaceLinkLibraries"}
)
FILE_API_QUERY_PARTS = (
    ".cmake",
    "api",
    "v1",
    "query",
    "client-asharia-target-truth",
)
FILE_API_QUERY_NAME = "query.json"
FILE_API_QUERY_BYTES = (
    json.dumps(
        {"requests": [{"kind": "codemodel", "version": 2}]},
        ensure_ascii=False,
        indent=2,
    )
    + "\n"
).encode("utf-8")


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8")


def _pointer_token(value: str) -> str:
    return value.replace("~", "~0").replace("/", "~1")


@dataclass(frozen=True, order=True)
class Diagnostic:
    document: str
    pointer: str
    code: str
    message: str

    def render(self) -> str:
        location = self.document + (f"#{self.pointer}" if self.pointer else "")
        return f"[{self.code}] {location}: {self.message}"


@dataclass(frozen=True)
class ManifestExpectation:
    name: str
    manifest_path: str
    pointer: str
    dependencies: frozenset[str]
    dependency_pointers: tuple[tuple[str, str], ...]
    is_test: bool

    def dependency_pointer(self, name: str) -> str:
        return next(
            (pointer for dependency, pointer in self.dependency_pointers if dependency == name),
            self.pointer,
        )


@dataclass(frozen=True)
class TargetRecord:
    target_id: str
    name: str
    imported: bool
    generator_provided: bool
    document: str
    data: dict[str, Any]

    @property
    def is_project_target(self) -> bool:
        return not self.imported and not self.generator_provided


@dataclass(frozen=True, order=True)
class RelationOrigin:
    field: str
    document: str
    pointer: str


@dataclass(frozen=True)
class CoverageSummary:
    manifest_count: int = 0
    target_count: int = 0
    test_target_count: int = 0
    configured_project_target_count: int = 0
    compared_target_count: int = 0
    missing_target_count: int = 0
    expected_edge_count: int = 0
    actual_edge_count: int = 0
    filtered_imported_relations: int = 0
    filtered_generator_relations: int = 0
    non_target_fragments: int = 0
    cmake_version: str = "unknown"
    codemodel_version: str = "unknown"
    configuration: str = "unknown"

    def render(self) -> str:
        fields = (
            ("manifests", self.manifest_count),
            ("targets", self.target_count),
            ("test-targets", self.test_target_count),
            ("configured-project-targets", self.configured_project_target_count),
            ("compared-targets", self.compared_target_count),
            ("missing-targets", self.missing_target_count),
            ("expected-edges", self.expected_edge_count),
            ("actual-edges", self.actual_edge_count),
            ("filtered-imported-relations", self.filtered_imported_relations),
            ("filtered-generator-relations", self.filtered_generator_relations),
            ("non-target-fragments", self.non_target_fragments),
            ("cmake", self.cmake_version),
            ("codemodel", self.codemodel_version),
            ("configuration", self.configuration),
        )
        return "Coverage: " + " ".join(f"{name}={value}" for name, value in fields)


@dataclass(frozen=True)
class AuditResult:
    summary: CoverageSummary
    diagnostics: tuple[Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return not self.diagnostics


class QueryPreparationError(RuntimeError):
    """The requested File API query path is not safe to mutate."""


def _add(
    diagnostics: list[Diagnostic],
    code: str,
    document: str,
    pointer: str,
    message: str,
) -> None:
    diagnostics.append(Diagnostic(document, pointer, code, message))


def _sorted(values: Iterable[Diagnostic]) -> tuple[Diagnostic, ...]:
    return tuple(sorted(values))


def _display(path: Path, root: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(root).as_posix()
    except ValueError:
        return resolved.as_posix()


def _absolute_without_link_resolution(path: Path) -> Path:
    return Path(os.path.abspath(os.fspath(path)))


def _path_kind(path: Path) -> tuple[bool, bool, bool]:
    """Return exists, is-directory, and is-link-or-reparse without following links."""

    try:
        metadata = os.lstat(path)
    except FileNotFoundError:
        return False, False, False
    except OSError as exc:
        raise QueryPreparationError(f"cannot inspect query path {path}: {exc}") from exc
    attributes = getattr(metadata, "st_file_attributes", 0)
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    linked = stat.S_ISLNK(metadata.st_mode) or bool(attributes & reparse_flag)
    return True, stat.S_ISDIR(metadata.st_mode), linked


def _is_regular_file(path: Path) -> bool:
    try:
        return stat.S_ISREG(os.lstat(path).st_mode)
    except FileNotFoundError:
        return False
    except OSError as exc:
        raise QueryPreparationError(f"cannot inspect query file {path}: {exc}") from exc


def _reject_linked_ancestors(path: Path) -> None:
    chain = list(reversed(path.parents)) + [path]
    for candidate in chain:
        exists, _, linked = _path_kind(candidate)
        if exists and linked:
            raise QueryPreparationError(
                f"query path contains a symlink or reparse point: {candidate}"
            )


def _ensure_real_directory(path: Path, *, create: bool) -> None:
    exists, directory, linked = _path_kind(path)
    if linked:
        raise QueryPreparationError(
            f"query path is a symlink or reparse point: {path}"
        )
    if exists and not directory:
        raise QueryPreparationError(f"query directory path is not a directory: {path}")
    if exists:
        return
    if not create:
        raise QueryPreparationError(f"build directory does not exist: {path}")
    try:
        path.mkdir()
    except FileExistsError:
        pass
    except OSError as exc:
        raise QueryPreparationError(f"cannot create query directory {path}: {exc}") from exc
    exists, directory, linked = _path_kind(path)
    if not exists or not directory or linked:
        raise QueryPreparationError(f"could not create a real directory: {path}")


def prepare_file_api_query(root: Path, build_directory: Path) -> Path:
    """Atomically install this tool's codemodel-major-2 File API query."""

    root = root.resolve()
    requested = build_directory if build_directory.is_absolute() else root / build_directory
    build_directory = _absolute_without_link_resolution(requested)
    build_root = root / "build"
    try:
        build_directory.relative_to(build_root)
        relative_build_directory = build_directory.relative_to(root)
    except ValueError as exc:
        raise QueryPreparationError(
            f"build directory must remain inside repository build root {build_root}: "
            f"{build_directory}"
        ) from exc
    _reject_linked_ancestors(build_directory)
    _ensure_real_directory(root, create=False)
    current_directory = root
    for part in relative_build_directory.parts:
        current_directory /= part
        _ensure_real_directory(current_directory, create=True)
    query_directory = build_directory
    for part in FILE_API_QUERY_PARTS:
        query_directory /= part
        _ensure_real_directory(query_directory, create=True)
    query_path = query_directory / FILE_API_QUERY_NAME
    exists, directory, linked = _path_kind(query_path)
    if linked:
        raise QueryPreparationError(
            f"query file is a symlink or reparse point: {query_path}"
        )
    if exists and directory:
        raise QueryPreparationError(f"query file path is a directory: {query_path}")
    if exists:
        if not _is_regular_file(query_path):
            raise QueryPreparationError(f"query file path is not a regular file: {query_path}")
        try:
            if query_path.read_bytes() == FILE_API_QUERY_BYTES:
                return query_path
        except OSError as exc:
            raise QueryPreparationError(f"cannot read query file {query_path}: {exc}") from exc

    descriptor = -1
    temporary_path: Path | None = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{FILE_API_QUERY_NAME}.",
            suffix=".tmp",
            dir=query_directory,
        )
        temporary_path = Path(temporary_name)
        with os.fdopen(descriptor, "wb") as stream:
            descriptor = -1
            stream.write(FILE_API_QUERY_BYTES)
            stream.flush()
            os.fsync(stream.fileno())
        _ensure_real_directory(query_directory, create=False)
        exists, directory, linked = _path_kind(query_path)
        if linked or (exists and (directory or not _is_regular_file(query_path))):
            raise QueryPreparationError(f"query destination became unsafe: {query_path}")
        os.replace(temporary_path, query_path)
        temporary_path = None
    except (OSError, QueryPreparationError) as exc:
        if isinstance(exc, QueryPreparationError):
            raise
        raise QueryPreparationError(f"cannot write query file {query_path}: {exc}") from exc
    finally:
        if descriptor >= 0:
            try:
                os.close(descriptor)
            except OSError:
                pass
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except OSError:
                pass
    return query_path


def _load_json(
    path: Path,
    root: Path,
    code: str,
    diagnostics: list[Diagnostic],
) -> tuple[bytes | None, Any | None]:
    try:
        exact_bytes = path.read_bytes()
        return exact_bytes, json.loads(exact_bytes.decode("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        _add(diagnostics, code, _display(path, root), "", str(exc))
        return None, None


def _string_list(
    value: Any,
    document: str,
    pointer: str,
    diagnostics: list[Diagnostic],
) -> list[str]:
    if not isinstance(value, list) or any(
        not isinstance(item, str) or not item for item in value
    ):
        _add(
            diagnostics,
            "truth.manifest.string-array-invalid",
            document,
            pointer,
            "value must be an array of non-empty strings",
        )
        return []
    if len(value) != len(set(value)):
        _add(
            diagnostics,
            "truth.manifest.duplicate-entry",
            document,
            pointer,
            "array contains duplicate entries",
        )
    return value


def _load_manifests(
    root: Path,
) -> tuple[dict[str, ManifestExpectation], int, int, list[Diagnostic]]:
    diagnostics: list[Diagnostic] = []
    expectations: dict[str, ManifestExpectation] = {}
    manifest_count = 0
    test_count = 0
    paths = sorted(
        (
            path
            for path in root.rglob(MANIFEST_NAME)
            if not any(
                part in IGNORED_PATH_PARTS for part in path.relative_to(root).parts
            )
        ),
        key=lambda path: _utf8_key(path.relative_to(root).as_posix()),
    )
    for path in paths:
        document = path.relative_to(root).as_posix()
        _, data = _load_json(path, root, "truth.manifest.unreadable", diagnostics)
        if not isinstance(data, dict):
            if data is not None:
                _add(
                    diagnostics,
                    "truth.manifest.root-invalid",
                    document,
                    "",
                    "manifest root must be an object",
                )
            continue
        if data.get("schemaVersion") != 1:
            continue
        manifest_count += 1
        if data.get("packageKind") != "source-boundary":
            _add(
                diagnostics,
                "truth.manifest.kind-invalid",
                document,
                "/packageKind",
                "schema v1 manifest must use packageKind 'source-boundary'",
            )
        targets = _string_list(data.get("targets"), document, "/targets", diagnostics)
        tests = _string_list(
            data.get("testTargets", []), document, "/testTargets", diagnostics
        )
        test_count += len(tests)
        declared = targets + tests
        if len(declared) != len(set(declared)):
            _add(
                diagnostics,
                "truth.manifest.target-overlap",
                document,
                "/targets",
                "targets and testTargets must not overlap",
            )
        raw = data.get("targetDependencies")
        if not isinstance(raw, dict):
            _add(
                diagnostics,
                "truth.manifest.dependencies-invalid",
                document,
                "/targetDependencies",
                "targetDependencies must be an object",
            )
            raw = {}
        declared_set = set(declared)
        keys = {key for key in raw if isinstance(key, str)}
        for name in sorted(declared_set - keys, key=_utf8_key):
            _add(
                diagnostics,
                "truth.manifest.target-key-missing",
                document,
                "/targetDependencies",
                f"targetDependencies is missing target '{name}'",
            )
        for name in sorted(keys - declared_set, key=_utf8_key):
            _add(
                diagnostics,
                "truth.manifest.target-key-extra",
                document,
                f"/targetDependencies/{_pointer_token(name)}",
                f"targetDependencies contains undeclared target '{name}'",
            )
        for name in declared:
            pointer = f"/targetDependencies/{_pointer_token(name)}"
            dependencies = _string_list(raw.get(name), document, pointer, diagnostics)
            if name in expectations:
                _add(
                    diagnostics,
                    "truth.manifest.target-duplicate",
                    document,
                    pointer,
                    f"target '{name}' is already owned by {expectations[name].manifest_path}",
                )
                continue
            expectations[name] = ManifestExpectation(
                name,
                document,
                pointer,
                frozenset(dependencies),
                tuple(
                    (dependency, f"{pointer}/{index}")
                    for index, dependency in enumerate(dependencies)
                ),
                name in tests,
            )
    if not manifest_count:
        _add(
            diagnostics,
            "truth.manifest.none",
            root.as_posix(),
            "",
            f"no schema v1 {MANIFEST_NAME} files were found",
        )
    return expectations, manifest_count, test_count, diagnostics


def _version(value: Any) -> tuple[int, int] | None:
    if not isinstance(value, dict):
        return None
    major, minor = value.get("major"), value.get("minor")
    if any(
        not isinstance(item, int) or isinstance(item, bool) or item < 0
        for item in (major, minor)
    ):
        return None
    return major, minor


def _cmake_version(index: Any) -> str:
    cmake = index.get("cmake") if isinstance(index, dict) else None
    version = cmake.get("version") if isinstance(cmake, dict) else None
    text = version.get("string") if isinstance(version, dict) else None
    return text if isinstance(text, str) and text else "unknown"


def _reply_path(
    reply_directory: Path,
    value: Any,
    document: str,
    pointer: str,
    diagnostics: list[Diagnostic],
) -> Path | None:
    valid = isinstance(value, str) and value and "\\" not in value
    parts = value.split("/") if valid else []
    pure = PurePosixPath(value) if valid else PurePosixPath()
    valid = valid and not pure.is_absolute() and not any(
        part in {"", ".", ".."} for part in parts
    )
    candidate = (reply_directory / Path(*pure.parts)).resolve() if valid else None
    valid = valid and candidate is not None and candidate.is_relative_to(reply_directory)
    if not valid:
        _add(
            diagnostics,
            "truth.cmake.reference-invalid",
            document,
            pointer,
            "jsonFile must remain inside the File API reply directory",
        )
        return None
    return candidate


def _select_codemodel(
    index: dict[str, Any],
    document: str,
    diagnostics: list[Diagnostic],
) -> tuple[dict[str, Any] | None, int | None, int | None]:
    objects = index.get("objects")
    if not isinstance(objects, list):
        _add(
            diagnostics,
            "truth.cmake.index-invalid",
            document,
            "/objects",
            "reply index objects must be an array",
        )
        return None, None, None
    available: list[int] = []
    candidates: list[tuple[int, int, dict[str, Any]]] = []
    for index, item in enumerate(objects):
        if not isinstance(item, dict) or item.get("kind") != "codemodel":
            continue
        item_version = _version(item.get("version"))
        if item_version is None or item_version[0] != 2:
            continue
        available.append(item_version[1])
        if item_version[1] >= MINIMUM_CODEMODEL_MINOR:
            candidates.append((item_version[1], index, item))
    if not candidates:
        suffix = f"; highest available minor is {max(available)}" if available else ""
        _add(
            diagnostics,
            "truth.cmake.codemodel-version-unsupported",
            document,
            "/objects",
            f"codemodel 2.{MINIMUM_CODEMODEL_MINOR}+ is required{suffix}",
        )
        return None, None, None
    minor = max(candidate[0] for candidate in candidates)
    selected = [candidate for candidate in candidates if candidate[0] == minor]
    if len(selected) != 1:
        _add(
            diagnostics,
            "truth.cmake.codemodel-ambiguous",
            document,
            "/objects",
            "exactly one highest-version codemodel object must be present",
        )
        return None, minor, None
    _, object_index, entry = selected[0]
    return entry, minor, object_index


def _load_records(
    root: Path,
    reply_index: Path,
    configuration: str,
) -> tuple[dict[str, TargetRecord], str, str, list[Diagnostic]]:
    diagnostics: list[Diagnostic] = []
    index_document = _display(reply_index, root)
    exact_index, index = _load_json(
        reply_index, root, "truth.cmake.index-unreadable", diagnostics
    )
    cmake_version = _cmake_version(index)
    if not isinstance(index, dict):
        if index is not None:
            _add(
                diagnostics,
                "truth.cmake.index-invalid",
                index_document,
                "",
                "reply index root must be an object",
            )
        return {}, cmake_version, "unknown", diagnostics
    entry, minor, object_index = _select_codemodel(index, index_document, diagnostics)
    codemodel_version = f"2.{minor}" if minor is not None else "unknown"
    if entry is None or object_index is None:
        return {}, cmake_version, codemodel_version, diagnostics
    reply_directory = reply_index.parent.resolve()
    codemodel_path = _reply_path(
        reply_directory,
        entry.get("jsonFile"),
        index_document,
        f"/objects/{object_index}/jsonFile",
        diagnostics,
    )
    if codemodel_path is None:
        return {}, cmake_version, codemodel_version, diagnostics
    _, codemodel = _load_json(
        codemodel_path, root, "truth.cmake.codemodel-unreadable", diagnostics
    )
    document = _display(codemodel_path, root)
    if not isinstance(codemodel, dict):
        if codemodel is not None:
            _add(
                diagnostics,
                "truth.cmake.codemodel-invalid",
                document,
                "",
                "codemodel root must be an object",
            )
        return {}, cmake_version, codemodel_version, diagnostics
    if codemodel.get("kind") != "codemodel":
        _add(
            diagnostics,
            "truth.cmake.codemodel-kind-mismatch",
            document,
            "/kind",
            "codemodel document kind must be 'codemodel'",
        )
    if _version(codemodel.get("version")) != (2, minor):
        _add(
            diagnostics,
            "truth.cmake.codemodel-version-mismatch",
            document,
            "/version",
            "codemodel version does not match the reply index",
        )
    paths = codemodel.get("paths")
    source = paths.get("source") if isinstance(paths, dict) else None
    if not isinstance(source, str) or not source:
        _add(
            diagnostics,
            "truth.cmake.source-root-invalid",
            document,
            "/paths/source",
            "codemodel source root must be a non-empty path",
        )
    elif os.path.normcase(str(Path(source).resolve())) != os.path.normcase(str(root)):
        _add(
            diagnostics,
            "truth.cmake.source-root-mismatch",
            document,
            "/paths/source",
            f"configured source root is '{Path(source).resolve()}', expected '{root}'",
        )
    configurations = codemodel.get("configurations")
    matches = (
        [
            (index, item)
            for index, item in enumerate(configurations)
            if isinstance(item, dict) and item.get("name") == configuration
        ]
        if isinstance(configurations, list)
        else []
    )
    if not isinstance(configurations, list) or len(matches) != 1:
        _add(
            diagnostics,
            "truth.cmake.configuration-mismatch",
            document,
            "/configurations",
            f"configuration '{configuration}' must appear exactly once",
        )
        return {}, cmake_version, codemodel_version, diagnostics
    configuration_index, selected = matches[0]
    summaries: list[tuple[dict[str, Any], str, bool]] = []
    for field, abstract in (("targets", False), ("abstractTargets", True)):
        values = selected.get(field)
        pointer = f"/configurations/{configuration_index}/{field}"
        if not isinstance(values, list):
            _add(
                diagnostics,
                "truth.cmake.target-summaries-invalid",
                document,
                pointer,
                f"{field} must be an array in codemodel 2.9+",
            )
            continue
        for summary_index, summary in enumerate(values):
            if isinstance(summary, dict):
                summaries.append((summary, f"{pointer}/{summary_index}", abstract))
            else:
                _add(
                    diagnostics,
                    "truth.cmake.target-summary-invalid",
                    document,
                    f"{pointer}/{summary_index}",
                    "target summary must be an object",
                )
    records: dict[str, TargetRecord] = {}
    for summary, pointer, expected_abstract in summaries:
        target_id, name = summary.get("id"), summary.get("name")
        if not isinstance(target_id, str) or not target_id or not isinstance(name, str) or not name:
            _add(
                diagnostics,
                "truth.cmake.target-summary-invalid",
                document,
                pointer,
                "target summary requires non-empty id and name strings",
            )
            continue
        target_path = _reply_path(
            reply_directory,
            summary.get("jsonFile"),
            document,
            f"{pointer}/jsonFile",
            diagnostics,
        )
        if target_path is None:
            continue
        _, target = _load_json(
            target_path, root, "truth.cmake.target-unreadable", diagnostics
        )
        target_document = _display(target_path, root)
        if not isinstance(target, dict):
            if target is not None:
                _add(
                    diagnostics,
                    "truth.cmake.target-invalid",
                    target_document,
                    "",
                    "target object must be an object",
                )
            continue
        flags = (
            target.get("imported", False),
            target.get("isGeneratorProvided", False),
            target.get("abstract", False),
        )
        if target.get("id") != target_id or target.get("name") != name:
            _add(
                diagnostics,
                "truth.cmake.target-identity-mismatch",
                target_document,
                "",
                f"target object does not match summary for '{name}'",
            )
            continue
        if not isinstance(target.get("type"), str) or not target["type"]:
            _add(
                diagnostics,
                "truth.cmake.target-type-invalid",
                target_document,
                "/type",
                "target type must be a non-empty string",
            )
        if _version(target.get("codemodelVersion")) != (2, minor):
            _add(
                diagnostics,
                "truth.cmake.target-version-mismatch",
                target_document,
                "/codemodelVersion",
                "target codemodelVersion does not match its codemodel",
            )
        if not all(isinstance(flag, bool) for flag in flags):
            _add(
                diagnostics,
                "truth.cmake.target-flag-invalid",
                target_document,
                "",
                "imported, isGeneratorProvided, and abstract must be booleans when present",
            )
            continue
        imported, generator_provided, abstract = flags
        if abstract != expected_abstract:
            _add(
                diagnostics,
                "truth.cmake.target-classification-mismatch",
                target_document,
                "/abstract",
                f"target '{name}' is listed in the wrong codemodel target array",
            )
        if target_id in records:
            _add(
                diagnostics,
                "truth.cmake.target-id-duplicate",
                target_document,
                "/id",
                "opaque target id appears more than once",
            )
            continue
        records[target_id] = TargetRecord(
            target_id, name, imported, generator_provided, target_document, target
        )
    try:
        final_index = reply_index.read_bytes()
    except OSError:
        final_index = None
    if exact_index is not None and final_index != exact_index:
        _add(
            diagnostics,
            "truth.cmake.reply-changed",
            index_document,
            "",
            "reply index changed while dependency evidence was read",
        )
    return records, cmake_version, codemodel_version, diagnostics


def _relations(
    records: dict[str, TargetRecord],
) -> tuple[
    dict[str, dict[str, tuple[RelationOrigin, ...]]], int, int, int, list[Diagnostic]
]:
    diagnostics: list[Diagnostic] = []
    result: dict[str, dict[str, tuple[RelationOrigin, ...]]] = {}
    imported_count = generator_count = fragment_count = 0
    for owner in sorted(records.values(), key=lambda item: _utf8_key(item.target_id)):
        collected: dict[str, list[RelationOrigin]] = {}
        for field in DIRECT_RELATION_FIELDS:
            values = owner.data.get(field, [])
            if not isinstance(values, list):
                _add(
                    diagnostics,
                    "truth.cmake.relations-invalid",
                    owner.document,
                    f"/{field}",
                    f"{field} must be an array when present",
                )
                continue
            for index, relation in enumerate(values):
                pointer = f"/{field}/{index}"
                if not isinstance(relation, dict):
                    _add(
                        diagnostics,
                        "truth.cmake.relation-invalid",
                        owner.document,
                        pointer,
                        "relation must be an object",
                    )
                    continue
                has_id, has_fragment = "id" in relation, "fragment" in relation
                valid_keys = (
                    has_id != has_fragment
                    if field in FRAGMENT_RELATION_FIELDS
                    else has_id and not has_fragment
                )
                if not valid_keys:
                    _add(
                        diagnostics,
                        "truth.cmake.relation-invalid",
                        owner.document,
                        pointer,
                        f"{field} relation has an invalid id/fragment shape",
                    )
                    continue
                if has_fragment:
                    if not isinstance(relation["fragment"], str) or not relation["fragment"]:
                        _add(
                            diagnostics,
                            "truth.cmake.relation-invalid",
                            owner.document,
                            f"{pointer}/fragment",
                            "fragment must be a non-empty string",
                        )
                    else:
                        fragment_count += 1
                    continue
                target_id = relation.get("id")
                dependency = records.get(target_id) if isinstance(target_id, str) else None
                if dependency is None:
                    _add(
                        diagnostics,
                        "truth.cmake.relation-dangling",
                        owner.document,
                        f"{pointer}/id",
                        f"relation from '{owner.name}' references an unknown target id",
                    )
                    continue
                injected = relation.get("fromDependency")
                if injected is not None and (
                    not isinstance(injected, dict)
                    or not isinstance(injected.get("id"), str)
                    or injected["id"] not in records
                ):
                    _add(
                        diagnostics,
                        "truth.cmake.from-dependency-invalid",
                        owner.document,
                        f"{pointer}/fromDependency",
                        "fromDependency must reference a known target id",
                    )
                if dependency.imported:
                    imported_count += 1
                elif dependency.generator_provided:
                    generator_count += 1
                else:
                    collected.setdefault(dependency.name, []).append(
                        RelationOrigin(field, owner.document, pointer)
                    )
        result[owner.target_id] = {
            name: tuple(sorted(origins))
            for name, origins in sorted(collected.items(), key=lambda item: _utf8_key(item[0]))
        }
    return result, imported_count, generator_count, fragment_count, diagnostics


def inspect_target_dependency_truth(
    root: Path,
    reply_index: Path,
    configuration: str,
) -> AuditResult:
    """Audit every v1 manifest target against one configured CMake graph."""

    root = root.resolve()
    reply_index = (
        reply_index.resolve()
        if reply_index.is_absolute()
        else (root / reply_index).resolve()
    )
    expectations, manifest_count, test_count, manifest_errors = _load_manifests(root)
    records, cmake_version, codemodel_version, cmake_errors = _load_records(
        root, reply_index, configuration
    )
    relation_map, imported, generated, fragments, relation_errors = _relations(records)
    project_records = [record for record in records.values() if record.is_project_target]
    diagnostics = manifest_errors + cmake_errors + relation_errors
    summary_values = dict(
        manifest_count=manifest_count,
        target_count=len(expectations),
        test_target_count=test_count,
        configured_project_target_count=len(project_records),
        expected_edge_count=sum(len(item.dependencies) for item in expectations.values()),
        filtered_imported_relations=imported,
        filtered_generator_relations=generated,
        non_target_fragments=fragments,
        cmake_version=cmake_version,
        codemodel_version=codemodel_version,
        configuration=configuration,
    )
    if diagnostics:
        return AuditResult(CoverageSummary(**summary_values), _sorted(diagnostics))

    project_by_name: dict[str, list[TargetRecord]] = {}
    for record in project_records:
        project_by_name.setdefault(record.name, []).append(record)
    matched: dict[str, TargetRecord] = {}
    missing_count = 0
    for name, expectation in sorted(expectations.items(), key=lambda item: _utf8_key(item[0])):
        candidates = project_by_name.get(name, [])
        if len(candidates) == 1:
            matched[name] = candidates[0]
            continue
        if not candidates:
            missing_count += 1
            kind = "test target" if expectation.is_test else "target"
            _add(
                diagnostics,
                "truth.manifest-target.missing",
                expectation.manifest_path,
                expectation.pointer,
                f"configured graph does not contain manifest {kind} '{name}'",
            )
        else:
            _add(
                diagnostics,
                "truth.manifest-target.ambiguous",
                expectation.manifest_path,
                expectation.pointer,
                f"configured graph contains {len(candidates)} project targets named '{name}'",
            )

    actual_edges = 0
    for name, record in sorted(matched.items(), key=lambda item: _utf8_key(item[0])):
        expectation = expectations[name]
        actual_relations = relation_map.get(record.target_id, {})
        actual = frozenset(actual_relations)
        actual_edges += len(actual)
        for dependency in sorted(expectation.dependencies - actual, key=_utf8_key):
            _add(
                diagnostics,
                "truth.dependency.manifest-only",
                expectation.manifest_path,
                expectation.dependency_pointer(dependency),
                f"'{name}' declares '{dependency}', but the configured graph does not",
            )
        for dependency in sorted(actual - expectation.dependencies, key=_utf8_key):
            origins = actual_relations[dependency]
            kinds = ", ".join(sorted({origin.field for origin in origins}, key=_utf8_key))
            _add(
                diagnostics,
                "truth.dependency.cmake-only",
                origins[0].document,
                origins[0].pointer,
                f"'{name}' depends on undeclared project target '{dependency}' via {kinds}",
            )
    summary = CoverageSummary(
        **summary_values,
        compared_target_count=len(matched),
        missing_target_count=missing_count,
        actual_edge_count=actual_edges,
    )
    return AuditResult(summary, _sorted(diagnostics))


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path, help="repository root")
    parser.add_argument(
        "--prepare-query",
        type=Path,
        metavar="BUILD_DIR",
        help="atomically prepare the owned File API query in BUILD_DIR",
    )
    parser.add_argument(
        "--reply-index",
        type=Path,
        help="File API reply index, absolute or relative to --root",
    )
    parser.add_argument(
        "--configuration",
        help="exact codemodel configuration name, for example Debug",
    )
    args = parser.parse_args(argv)
    if args.prepare_query is not None:
        if args.reply_index is not None or args.configuration is not None:
            parser.error(
                "--prepare-query cannot be combined with --reply-index or --configuration"
            )
    elif args.reply_index is None or args.configuration is None:
        parser.error("audit mode requires --reply-index and --configuration")
    return args


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    if args.prepare_query is not None:
        try:
            query_path = prepare_file_api_query(args.root, args.prepare_query)
        except (QueryPreparationError, OSError) as exc:
            print(f"Target dependency truth query preparation failed: {exc}", file=sys.stderr)
            return 1
        print(f"Target dependency truth query ready: {query_path}")
        return 0
    assert args.reply_index is not None
    assert args.configuration is not None
    result = inspect_target_dependency_truth(
        args.root, args.reply_index, args.configuration
    )
    if result.diagnostics:
        print(
            f"Target dependency truth validation failed with {len(result.diagnostics)} error(s):",
            file=sys.stderr,
        )
        for diagnostic in result.diagnostics:
            print(f"  - {diagnostic.render()}", file=sys.stderr)
        print(result.summary.render(), file=sys.stderr)
        return 1
    print(f"Target dependency truth OK. {result.summary.render()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
