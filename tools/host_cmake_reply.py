"""Read one stable final CMake File API reply for a generated Host."""

from __future__ import annotations

import json
import stat
import unicodedata
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_cmake_query as query


@dataclass(frozen=True)
class HostCMakeReplyEvidence:
    build_root: Path
    reply_index_path: Path
    configuration: str
    target_name: str
    target: dict[str, Any]
    codemodel_major: int
    codemodel_minor: int


class ReplyFailure(Exception):
    def __init__(self, diagnostics: Iterable[contracts.Diagnostic]) -> None:
        super().__init__()
        self.diagnostics = tuple(diagnostics)


class _TransientReply(Exception):
    pass


def _fail(code: str, pointer: str, message: str) -> ReplyFailure:
    return ReplyFailure([query.diagnostic(code, pointer, message)])


def _safe_reference(value: Any, pointer: str) -> str:
    if not isinstance(value, str) or unicodedata.normalize("NFC", value) != value:
        raise _fail(
            "host-build.cmake-reply-reference-invalid",
            pointer,
            "reply jsonFile must be one normalized relative filename",
        )
    path = PurePosixPath(value)
    if (
        not value
        or path.is_absolute()
        or len(path.parts) != 1
        or path.name != value
        or value in {".", ".."}
        or "\\" in value
        or ":" in value
    ):
        raise _fail(
            "host-build.cmake-reply-reference-invalid",
            pointer,
            "reply jsonFile must stay inside the explicit reply directory",
        )
    return value


def _read_json(path: Path, pointer: str, code: str) -> tuple[bytes, Any]:
    try:
        status = path.lstat()
        if query.is_link_or_reparse(status) or not stat.S_ISREG(status.st_mode):
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


def _codemodel_response(index: Any) -> tuple[str, int, int]:
    reply = index.get("reply") if isinstance(index, dict) else None
    client = reply.get(query.HOST_BUILD_FILE_API_CLIENT) if isinstance(reply, dict) else None
    stateful = client.get("query.json") if isinstance(client, dict) else None
    responses = stateful.get("responses") if isinstance(stateful, dict) else None
    if not isinstance(responses, list) or len(responses) != 1:
        raise _fail(
            "host-build.cmake-query-response-missing",
            "/reply",
            "latest reply must contain one Host codemodel response",
        )
    response = responses[0]
    version = response.get("version") if isinstance(response, dict) else None
    major = version.get("major") if isinstance(version, dict) else None
    minor = version.get("minor") if isinstance(version, dict) else None
    if (
        not isinstance(response, dict)
        or response.get("kind") != "codemodel"
        or major != query.HOST_BUILD_FILE_API_MAJOR
        or not isinstance(minor, int)
        or isinstance(minor, bool)
        or minor < query.HOST_BUILD_FILE_API_MINOR
    ):
        raise _fail(
            "host-build.cmake-codemodel-version-unsupported",
            "/reply",
            "Host target reader requires a successful codemodel 2.6 or newer reply",
        )
    reference = _safe_reference(response.get("jsonFile"), "/reply/jsonFile")
    return reference, major, minor


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
    try:
        build_root = Path(value).resolve(strict=True)
    except OSError:
        raise _fail(
            "host-build.cmake-build-root-mismatch",
            "/codemodel/paths/build",
            "codemodel build path must resolve to the explicit build root",
        ) from None
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
) -> tuple[Path, dict[str, Any]]:
    target_id = summary.get("id")
    if not isinstance(target_id, str) or not target_id:
        raise _fail(
            "host-build.cmake-target-identity-mismatch",
            "/codemodel/configurations/targets/id",
            f"target '{name}' summary has no stable opaque id",
        )
    reference = _safe_reference(
        summary.get("jsonFile"),
        "/codemodel/configurations/targets/jsonFile",
    )
    path = reply_root / reference
    _, target = _read_json(
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
    return path, target


def _verify_stable(
    reply_root: Path,
    index_path: Path,
    index_bytes: bytes,
    references: tuple[Path, ...],
) -> None:
    try:
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
    for path in references:
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
        if query.is_link_or_reparse(status) or not stat.S_ISREG(status.st_mode):
            raise _fail(
                "host-build.cmake-reply-unreadable",
                "/reply",
                "referenced CMake File API JSON is not regular",
            )


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
    codemodel_name, major, minor = _codemodel_response(index)
    codemodel_path = reply_root / codemodel_name
    _, codemodel = _read_json(
        codemodel_path,
        "/codemodel",
        "host-build.cmake-codemodel-unreadable",
    )
    validated_root = _codemodel_root(codemodel, build_root)
    summary = _target_summary(_configuration(codemodel, configuration), target_name)
    target_path, target = _target_object(reply_root, summary, target_name)
    _verify_stable(reply_root, index_path, index_bytes, (codemodel_path, target_path))
    return HostCMakeReplyEvidence(
        validated_root,
        index_path,
        configuration,
        target_name,
        target,
        major,
        minor,
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
    raise _fail(
        "host-build.cmake-reply-unstable",
        "/reply",
        "CMake File API reply changed during every bounded read attempt",
    )
