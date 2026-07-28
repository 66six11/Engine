"""Validate stateful Host CMake File API response structure."""

from __future__ import annotations

import unicodedata
from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Any

from tools import check_package_contracts as contracts
from tools import host_cmake_query as query


@dataclass(frozen=True)
class HostCMakeObjectReference:
    json_file: str
    major: int
    minor: int


@dataclass(frozen=True)
class HostCMakeResponseReferences:
    generator_name: str
    generator_multi_config: bool
    codemodel: HostCMakeObjectReference
    toolchains: HostCMakeObjectReference


class ResponseFailure(Exception):
    def __init__(self, diagnostic: contracts.Diagnostic) -> None:
        super().__init__()
        self.diagnostic = diagnostic


def _fail(code: str, pointer: str, message: str) -> ResponseFailure:
    return ResponseFailure(query.diagnostic(code, pointer, message))


def safe_reference(value: Any, pointer: str) -> str:
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


def _generator(index: Any) -> tuple[str, bool]:
    cmake = index.get("cmake") if isinstance(index, dict) else None
    generator = cmake.get("generator") if isinstance(cmake, dict) else None
    name = generator.get("name") if isinstance(generator, dict) else None
    multi_config = (
        generator.get("multiConfig") if isinstance(generator, dict) else None
    )
    if (
        not isinstance(name, str)
        or not name
        or unicodedata.normalize("NFC", name) != name
        or any(marker in name for marker in ("/", "\\", ":", "\r", "\n"))
        or not isinstance(multi_config, bool)
    ):
        raise _fail(
            "host-build.cmake-generator-invalid",
            "/cmake/generator",
            "reply index must identify one stable CMake generator",
        )
    return name, multi_config


def _object_reference(
    response: Any,
    response_index: int,
    kind: str,
    required_major: int,
    required_minor: int,
) -> HostCMakeObjectReference:
    version = response.get("version") if isinstance(response, dict) else None
    major = version.get("major") if isinstance(version, dict) else None
    minor = version.get("minor") if isinstance(version, dict) else None
    if (
        not isinstance(response, dict)
        or response.get("kind") != kind
        or major != required_major
        or not isinstance(minor, int)
        or isinstance(minor, bool)
        or minor < required_minor
    ):
        raise _fail(
            f"host-build.cmake-{kind}-version-unsupported",
            f"/reply/responses/{response_index}",
            f"Host target reader requires a successful {kind} "
            f"{required_major}.{required_minor} or newer reply",
        )
    return HostCMakeObjectReference(
        safe_reference(
            response.get("jsonFile"),
            f"/reply/responses/{response_index}/jsonFile",
        ),
        major,
        minor,
    )


def read_host_response_references(index: Any) -> HostCMakeResponseReferences:
    """Validate the exact query echo and its two ordered responses."""

    reply = index.get("reply") if isinstance(index, dict) else None
    client = (
        reply.get(query.HOST_BUILD_FILE_API_CLIENT)
        if isinstance(reply, dict)
        else None
    )
    stateful = client.get("query.json") if isinstance(client, dict) else None
    requests = stateful.get("requests") if isinstance(stateful, dict) else None
    responses = stateful.get("responses") if isinstance(stateful, dict) else None
    expected_requests = [
        {
            "kind": "codemodel",
            "version": {
                "major": query.HOST_BUILD_FILE_API_MAJOR,
                "minor": query.HOST_BUILD_FILE_API_MINOR,
            },
        },
        {
            "kind": "toolchains",
            "version": {
                "major": query.HOST_BUILD_TOOLCHAINS_FILE_API_MAJOR,
                "minor": query.HOST_BUILD_TOOLCHAINS_FILE_API_MINOR,
            },
        },
    ]
    if requests != expected_requests:
        raise _fail(
            "host-build.cmake-query-mismatch",
            "/reply/requests",
            "latest reply does not mirror the exact Host File API query",
        )
    if not isinstance(responses, list) or len(responses) != 2:
        raise _fail(
            "host-build.cmake-query-response-missing",
            "/reply/responses",
            "latest reply must contain the two requested Host responses",
        )
    generator_name, generator_multi_config = _generator(index)
    return HostCMakeResponseReferences(
        generator_name,
        generator_multi_config,
        _object_reference(
            responses[0],
            0,
            "codemodel",
            query.HOST_BUILD_FILE_API_MAJOR,
            query.HOST_BUILD_FILE_API_MINOR,
        ),
        _object_reference(
            responses[1],
            1,
            "toolchains",
            query.HOST_BUILD_TOOLCHAINS_FILE_API_MAJOR,
            query.HOST_BUILD_TOOLCHAINS_FILE_API_MINOR,
        ),
    )


def validate_toolchains_object(
    toolchains: Any,
    reference: HostCMakeObjectReference,
) -> dict[str, Any]:
    version = toolchains.get("version") if isinstance(toolchains, dict) else None
    entries = toolchains.get("toolchains") if isinstance(toolchains, dict) else None
    if (
        not isinstance(toolchains, dict)
        or toolchains.get("kind") != "toolchains"
        or not isinstance(version, dict)
        or version.get("major") != reference.major
        or version.get("minor") != reference.minor
        or not isinstance(entries, list)
    ):
        raise _fail(
            "host-build.cmake-toolchains-malformed",
            "/toolchains",
            "referenced toolchains object does not match its reply reference",
        )
    return toolchains
