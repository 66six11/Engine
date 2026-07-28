"""Read one stable final CMake File API reply for a generated Host."""

from __future__ import annotations

import json
import stat
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_cmake_query as query
from tools import host_cmake_response as response_contract


@dataclass(frozen=True)
class HostCMakeReplyEvidence:
    build_root: Path
    reply_index_path: Path
    configuration: str
    target_name: str
    target: dict[str, Any]
    toolchains: dict[str, Any]
    generator_name: str
    generator_multi_config: bool
    codemodel_major: int
    codemodel_minor: int
    toolchains_major: int
    toolchains_minor: int


class ReplyFailure(Exception):
    def __init__(self, diagnostics: Iterable[contracts.Diagnostic]) -> None:
        super().__init__()
        self.diagnostics = tuple(diagnostics)


class _TransientReply(Exception):
    pass


def _fail(code: str, pointer: str, message: str) -> ReplyFailure:
    return ReplyFailure([query.diagnostic(code, pointer, message)])


def _read_json(path: Path, pointer: str, code: str) -> tuple[bytes, Any]:
    try:
        status = path.lstat()
        if (
            query.is_link_or_reparse(status)
            or not stat.S_ISREG(status.st_mode)
            or query.resolve_existing_regular_file_without_links(path) is None
        ):
            raise _fail(code, pointer, "referenced CMake File API JSON is not regular")
        content = path.read_bytes()
    except FileNotFoundError as error:
        raise _TransientReply from error
    except OSError:
        raise _fail(code, pointer, "referenced CMake File API JSON is unreadable") from None
    try:
        return content, json.loads(content.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        raise _fail(code, pointer, "referenced CMake File API JSON is malformed") from None


def _latest_index(reply_root: Path) -> Path | None:
    if query.resolve_existing_directory_without_links(reply_root) is None:
        return None
    try:
        candidates = tuple(reply_root.glob("index-*.json"))
    except OSError:
        return None
    return max(candidates, key=lambda value: value.name) if candidates else None


def _validate_cmake_version(index: Any) -> None:
    cmake = index.get("cmake") if isinstance(index, dict) else None
    version = cmake.get("version") if isinstance(cmake, dict) else None
    components = (
        version.get("major") if isinstance(version, dict) else None,
        version.get("minor") if isinstance(version, dict) else None,
        version.get("patch") if isinstance(version, dict) else None,
    )
    if any(not isinstance(value, int) or isinstance(value, bool) for value in components):
        raise _fail(
            "host-build.cmake-version-unsupported",
            "/cmake/version",
            "reply index must identify CMake 3.28 or newer",
        )
    major, minor, _ = components
    assert isinstance(major, int)
    assert isinstance(minor, int)
    if (major, minor) < query.HOST_BUILD_MINIMUM_CMAKE_VERSION:
        raise _fail(
            "host-build.cmake-version-unsupported",
            "/cmake/version",
            "reply index must identify CMake 3.28 or newer",
        )


def _codemodel_root(codemodel: Any, expected_root: Path) -> Path:
    version = codemodel.get("version") if isinstance(codemodel, dict) else None
    configurations = codemodel.get("configurations") if isinstance(codemodel, dict) else None
    if (
        not isinstance(codemodel, dict)
        or codemodel.get("kind") != "codemodel"
        or not isinstance(version, dict)
        or version.get("major") != query.HOST_BUILD_FILE_API_MAJOR
        or not isinstance(version.get("minor"), int)
        or isinstance(version.get("minor"), bool)
        or version["minor"] < query.HOST_BUILD_FILE_API_MINOR
        or not isinstance(configurations, list)
    ):
        raise _fail(
            "host-build.cmake-codemodel-malformed",
            "/codemodel",
            "referenced codemodel does not satisfy version 2.6",
        )
    paths = codemodel.get("paths")
    value = paths.get("build") if isinstance(paths, dict) else None
    if (
        not isinstance(value, str)
        or not value
        or unicodedata.normalize("NFC", value) != value
        or "\\" in value
        or not Path(value).is_absolute()
    ):
        raise _fail(
            "host-build.cmake-build-root-mismatch",
            "/codemodel/paths/build",
            "codemodel build path must be absolute and match the explicit build root",
        )
    build_root = query.resolve_existing_directory_without_links(Path(value))
    if build_root is None:
        raise _fail(
            "host-build.cmake-build-root-mismatch",
            "/codemodel/paths/build",
            "codemodel build path must resolve to the explicit build root",
        )
    if build_root != expected_root:
        raise _fail(
            "host-build.cmake-build-root-mismatch",
            "/codemodel/paths/build",
            "codemodel build path resolves to a different build root",
        )
    return build_root


def _configuration(codemodel: dict[str, Any], name: str) -> dict[str, Any]:
    matches = [
        value
        for value in codemodel["configurations"]
        if isinstance(value, dict) and value.get("name") == name
    ]
    if len(matches) != 1:
        raise _fail(
            "host-build.cmake-configuration-mismatch",
            "/codemodel/configurations",
            f"configuration '{name}' must appear exactly once",
        )
    return matches[0]


def _target_summary(configuration: dict[str, Any], name: str) -> dict[str, Any]:
    targets = configuration.get("targets")
    matches = (
        [
            value
            for value in targets
            if isinstance(value, dict) and value.get("name") == name
        ]
        if isinstance(targets, list)
        else []
    )
    if len(matches) != 1:
        raise _fail(
            "host-build.cmake-target-mismatch",
            "/codemodel/configurations/targets",
            f"target '{name}' must appear exactly once",
        )
    return matches[0]


def _target_object(
    reply_root: Path,
    summary: dict[str, Any],
    name: str,
) -> tuple[Path, bytes, dict[str, Any]]:
    target_id = summary.get("id")
    if not isinstance(target_id, str) or not target_id:
        raise _fail(
            "host-build.cmake-target-identity-mismatch",
            "/codemodel/configurations/targets/id",
            f"target '{name}' summary has no stable opaque id",
        )
    reference = response_contract.safe_reference(
        summary.get("jsonFile"),
        "/codemodel/configurations/targets/jsonFile",
    )
    path = reply_root / reference
    content, target = _read_json(
        path,
        "/codemodel/target",
        "host-build.cmake-target-unreadable",
    )
    if (
        not isinstance(target, dict)
        or target.get("id") != target_id
        or target.get("name") != name
    ):
        raise _fail(
            "host-build.cmake-target-identity-mismatch",
            "/codemodel/target",
            f"target object identity does not match '{name}' summary",
        )
    return path, content, target


def _verify_stable(
    reply_root: Path,
    index_path: Path,
    index_bytes: bytes,
    references: tuple[tuple[Path, bytes], ...],
) -> None:
    try:
        if query.resolve_existing_regular_file_without_links(index_path) is None:
            raise OSError
        final_index_bytes = index_path.read_bytes()
    except FileNotFoundError as error:
        raise _TransientReply from error
    except OSError:
        raise _fail(
            "host-build.cmake-index-unreadable",
            "/reply",
            "CMake File API reply index became unreadable",
        ) from None
    if final_index_bytes != index_bytes or _latest_index(reply_root) != index_path:
        raise _TransientReply
    for path, expected_content in references:
        try:
            status = path.lstat()
        except FileNotFoundError as error:
            raise _TransientReply from error
        except OSError:
            raise _fail(
                "host-build.cmake-reply-unreadable",
                "/reply",
                "referenced CMake File API JSON became unreadable",
            ) from None
        if (
            query.is_link_or_reparse(status)
            or not stat.S_ISREG(status.st_mode)
            or query.resolve_existing_regular_file_without_links(path) is None
        ):
            raise _fail(
                "host-build.cmake-reply-unreadable",
                "/reply",
                "referenced CMake File API JSON is not regular",
            )
        try:
            if path.read_bytes() != expected_content:
                raise _TransientReply
        except FileNotFoundError as error:
            raise _TransientReply from error
        except OSError:
            raise _fail(
                "host-build.cmake-reply-unreadable",
                "/reply",
                "referenced CMake File API JSON became unreadable",
            ) from None


def _read_once(build_root: Path, configuration: str, target_name: str) -> HostCMakeReplyEvidence:
    reply_root = build_root / query.REPLY_RELATIVE_PATH
    index_path = _latest_index(reply_root)
    if index_path is None:
        raise _fail(
            "host-build.cmake-index-missing",
            "/reply",
            "final configure produced no CMake File API reply index",
        )
    index_bytes, index = _read_json(
        index_path,
        "/reply",
        "host-build.cmake-index-unreadable",
    )
    _validate_cmake_version(index)
    references = response_contract.read_host_response_references(index)
    codemodel_path = reply_root / references.codemodel.json_file
    codemodel_bytes, codemodel = _read_json(
        codemodel_path,
        "/codemodel",
        "host-build.cmake-codemodel-unreadable",
    )
    validated_root = _codemodel_root(codemodel, build_root)
    summary = _target_summary(_configuration(codemodel, configuration), target_name)
    target_path, target_bytes, target = _target_object(
        reply_root,
        summary,
        target_name,
    )
    toolchains_path = reply_root / references.toolchains.json_file
    toolchains_bytes, toolchains = _read_json(
        toolchains_path,
        "/toolchains",
        "host-build.cmake-toolchains-unreadable",
    )
    validated_toolchains = response_contract.validate_toolchains_object(
        toolchains,
        references.toolchains,
    )
    _verify_stable(
        reply_root,
        index_path,
        index_bytes,
        (
            (codemodel_path, codemodel_bytes),
            (target_path, target_bytes),
            (toolchains_path, toolchains_bytes),
        ),
    )
    return HostCMakeReplyEvidence(
        validated_root,
        index_path,
        configuration,
        target_name,
        target,
        validated_toolchains,
        references.generator_name,
        references.generator_multi_config,
        references.codemodel.major,
        references.codemodel.minor,
        references.toolchains.major,
        references.toolchains.minor,
    )


def read_stable_reply(
    build_root: Path,
    configuration: str,
    target_name: str,
) -> HostCMakeReplyEvidence:
    for _ in range(query.HOST_BUILD_FILE_API_READ_ATTEMPTS):
        try:
            return _read_once(build_root, configuration, target_name)
        except _TransientReply:
            # CMake may delete old reply files while publishing a newer index.
            continue
        except response_contract.ResponseFailure as failure:
            raise ReplyFailure([failure.diagnostic]) from None
    raise _fail(
        "host-build.cmake-reply-unstable",
        "/reply",
        "CMake File API reply changed during every bounded read attempt",
    )
